// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef STORAGESERVICE_H
#define STORAGESERVICE_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// Cross-pool arbitration contains no provider calls, callbacks or waits. Pools
// acquire a permit BEFORE occupying a worker with I/O. A queued unknown identity
// eventually drains all lanes, since it could alias any of them.
class StorageService
{
public:
	enum class Kind
	{
		Read,
		Write
	};
	static constexpr uint64_t kDirtyAgeMs = 1000;
	static constexpr unsigned kMaxBurst = 4;
	static uint64_t Now()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
						     .count());
	}

private:
	struct Task
	{
		std::string key, raw;
		Kind kind;
		uint64_t deadline, sequence;
		bool active = false;
		bool registered = false;
	};
	struct Lane
	{
		std::list<Task *> waiting;
		Task *active = nullptr;
		Kind last = Kind::Read;
		unsigned burst = 0;
	};
	struct State
	{
		std::mutex mutex;
		std::map<std::string, Lane> lanes;
		uint64_t sequence = 0;
		std::map<std::string, std::string> aliases, pendingAliases;
		bool aliasesReady = false, aliasesPending = false;
		size_t active = 0;
	};

public:
	class Ticket
	{
		friend class StorageService;

	public:
		~Ticket()
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (!task.registered) {
				return;
			}
			auto found = state->lanes.find(task.key);
			auto &lane = found->second;
			if (task.active) {
				lane.active = nullptr;
				--state->active;
			} else {
				lane.waiting.remove(&task);
			}
			if (lane.active == nullptr && lane.waiting.empty()) {
				state->lanes.erase(found);
			}
		}
		Ticket(const Ticket &) = delete;
		Ticket &operator=(const Ticket &) = delete;

	private:
		Ticket(std::shared_ptr<State> owner, std::string key, Kind kind, uint64_t deadline)
		: state(std::move(owner))
		, task{ key, std::move(key), kind, deadline, 0 }
		{
		}
		std::shared_ptr<State> state;
		Task task;
	};
	using Permit = std::shared_ptr<Ticket>;
	explicit StorageService(bool resolveAliases = false)
	: m_resolveAliases(resolveAliases)
	{
	}
	static StorageService &Get()
	{
#ifdef __linux__
		static StorageService service(true);
#else
		static StorageService service;
#endif
		return service;
	}
	// A mount snapshot also identifies partitions sharing a physical device.
	// Keep active keys until their calls finish and reclassify only queued tickets.
	void SetAliases(std::map<std::string, std::string> aliases)
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (m_state->aliasesReady && aliases == m_state->aliases && !m_state->aliasesPending) {
			return;
		}
		m_state->pendingAliases = std::move(aliases);
		m_state->aliasesPending = true;
	}

	Permit Queue(std::string key, Kind kind, uint64_t deadline)
	{
		if (key.empty()) {
			key = "unknown";
		}
		// Windows volume GUIDs are case insensitive; both providers use this spelling.
		for (auto &c : key) {
			if (c >= 'A' && c <= 'Z') {
				c = static_cast<char>(c + ('a' - 'A'));
			}
		}
		auto ticket = Permit(new Ticket(m_state, std::move(key), kind, deadline));
		std::lock_guard<std::mutex> lock(m_state->mutex);
		ticket->task.key = Resource(ticket->task.raw);
		ticket->task.sequence = ++m_state->sequence;
		m_state->lanes[ticket->task.key].waiting.push_back(&ticket->task);
		ticket->task.registered = true;
		return ticket;
	}
	bool TryStart(const Permit &ticket, uint64_t now = Now())
	{
		if (!ticket) {
			return true;
		} // non-storage discovery jobs
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (m_state->aliasesPending) {
			std::list<Task *> waiting;
			for (auto it = m_state->lanes.begin(); it != m_state->lanes.end();) {
				waiting.splice(waiting.end(), it->second.waiting);
				if (it->second.active == nullptr) {
					it = m_state->lanes.erase(it);
				} else {
					++it;
				}
			}
			m_state->aliases = std::move(m_state->pendingAliases);
			m_state->aliasesPending = false;
			m_state->aliasesReady = true;
			for (auto *task : waiting) {
				task->key = Resource(task->raw);
				m_state->lanes[task->key].waiting.push_back(task);
			}
		}
		auto &task = ticket->task;
		auto &lane = m_state->lanes.at(task.key);
		if (task.active || lane.active != nullptr) {
			return false;
		}
		// A remapped active call still owns its previous resource, and also
		// blocks aliases discovered by the new snapshot. Unaffected storage
		// does not wait for that call merely because a mount was added.
		for (const auto &entry : m_state->lanes) {
			const auto *active = entry.second.active;
			if (active != nullptr) {
				const auto current = Resource(active->raw);
				if (task.key == "unknown" || active->key == "unknown" ||
					current == "unknown" || task.key == active->key ||
					task.key == current) {
					return false;
				}
			}
		}
		const auto unknown = m_state->lanes.find("unknown");
		if (task.key == "unknown") {
			if (m_state->active != 0) {
				return false;
			}
		} else if (unknown != m_state->lanes.end()) {
			if (unknown->second.active != nullptr) {
				return false;
			}
			// Prevent a stream of known work from starving an unresolved alias.
			for (const auto *waiting : unknown->second.waiting) {
				if (waiting->deadline <= now) {
					return false;
				}
			}
		}
		Task *best = nullptr;
		const bool forceOther =
			lane.burst >= kMaxBurst &&
			std::any_of(lane.waiting.begin(), lane.waiting.end(), [&](const Task *pending) {
				return pending->kind != lane.last;
			});
		for (auto *pending : lane.waiting) {
			if (forceOther && pending->kind == lane.last) {
				continue;
			}
			if (best == nullptr || pending->deadline < best->deadline ||
				(pending->deadline == best->deadline && pending->sequence < best->sequence)) {
				best = pending;
			}
		}
		if (best != &task) {
			return false;
		}
		lane.waiting.remove(&task);
		lane.burst = lane.last == task.kind ? std::min(kMaxBurst, lane.burst + 1) : 1;
		lane.last = task.kind;
		lane.active = &task;
		task.active = true;
		++m_state->active;
		return true;
	}
	// Tests/diagnostics can verify that drained storage does not leave history.
	size_t Accounts() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->lanes.size();
	}

private:
	std::string Resource(const std::string &raw) const
	{
		if (!m_resolveAliases) {
			return raw;
		}
		const auto found = m_state->aliases.find(raw);
		return found == m_state->aliases.end() ? "unknown" : found->second;
	}
	bool m_resolveAliases;
	std::shared_ptr<State> m_state = std::make_shared<State>();
};
#endif
