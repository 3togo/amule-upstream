// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VOLUMEWRITEPOOL_H
#define VOLUMEWRITEPOOL_H

#include <array>
#include <condition_variable>
#include <exception>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Discovery and writes run outside the scheduler lock. Resolving a directory
// releases its worker before waiting for a volume, so aliases never consume all
// workers waiting behind an active write. Only one batch per resolved volume runs.
// Unknown identities conservatively share a write lane. At most four provider
// calls can block; shutdown drains accepted ownership and never detaches workers.
template <typename Job> class VolumeWritePool
{
public:
	static constexpr size_t kWorkers = 4;
	static constexpr size_t kMaxJobs = 4096;
	static constexpr size_t kBatchSize = 64;
	using Batch = std::vector<Job>;
	using Failure = std::function<void(Batch &, std::exception_ptr)>;

	VolumeWritePool(
		std::function<std::string(Job &)> resolve,
		std::function<void(Batch &)> work,
		Failure failure,
		std::function<bool(Job &)> discard = [](Job &) { return false; })
	: m_resolve(std::move(resolve))
	, m_work(std::move(work))
	, m_failure(std::move(failure))
	, m_discard(std::move(discard))
	{
		try {
			for (auto &worker : m_workers) {
				worker = std::thread([this] { Run(); });
			}
		} catch (...) {
			Stop();
			throw;
		}
	}
	~VolumeWritePool() { Stop(); }
	VolumeWritePool(const VolumeWritePool &) = delete;
	VolumeWritePool &operator=(const VolumeWritePool &) = delete;

	bool Submit(const std::string &path, Job job)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping || m_owned == kMaxJobs) {
			return false;
		}
		const auto found = m_volumes.find(path);
		m_queue.push_back({ path,
			found == m_volumes.end() ? "" : found->second,
			std::move(job),
			++m_sequence });
		++m_owned;
		m_ready.notify_one();
		return true;
	}

	// Active/probing jobs keep ownership until their callback returns. Callers
	// also revoke their per-file token before cancelling queued jobs.
	template <typename Predicate, typename Cancel> void CancelQueued(Predicate predicate, Cancel cancel)
	{
		Batch removed;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (auto it = m_queue.begin(); it != m_queue.end();) {
				if (predicate(it->job)) {
					removed.push_back(std::move(it->job));
					it = m_queue.erase(it);
				} else {
					++it;
				}
			}
		}
		for (auto &job : removed) {
			cancel(job);
		}
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_owned -= removed.size();
		}
		m_ready.notify_all();
	}

	void Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_ready.notify_all();
		for (auto &worker : m_workers) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}

private:
	struct Item
	{
		std::string path, volume;
		Job job;
		size_t sequence;
	};
	using Iterator = typename std::list<Item>::iterator;
	Iterator Next()
	{
		for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
			if (!it->volume.empty() && m_active.count(it->volume) == 0) {
				return it;
			}
		}
		for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
			if (it->volume.empty() && m_resolving.count(it->path) == 0) {
				return it;
			}
		}
		return m_queue.end();
	}
	void Run()
	{
		for (;;) {
			Batch batch;
			std::string volume;
			std::list<Item> probe;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				Iterator next;
				m_ready.wait(lock, [&] {
					next = Next();
					return next != m_queue.end() || (m_stopping && m_owned == 0);
				});
				if (next == m_queue.end()) {
					return;
				}
				volume = next->volume;
				if (volume.empty()) {
					m_resolving.insert(next->path);
					probe.splice(probe.end(), m_queue, next);
				} else {
					m_active.insert(volume);
					for (auto it = m_queue.begin();
						it != m_queue.end() && batch.size() < kBatchSize;) {
						if (it->volume == volume) {
							batch.push_back(std::move(it->job));
							it = m_queue.erase(it);
						} else {
							++it;
						}
					}
				}
			}
			if (!probe.empty()) {
				auto &item = probe.front();
				std::exception_ptr error;
				try {
					volume = m_resolve(item.job);
					if (volume.empty()) {
						volume = "unknown";
					}
				} catch (...) {
					error = std::current_exception();
					batch.push_back(std::move(item.job));
					m_failure(batch, error);
				}
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_resolving.erase(item.path);
					if (!error) {
						m_volumes[item.path] = volume;
						for (auto &queued : m_queue) {
							if (queued.path == item.path) {
								queued.volume = volume;
							}
						}
					}
					if (error || m_discard(item.job)) {
						--m_owned;
					} else {
						item.volume = volume;
						auto position = m_queue.begin();
						while (position != m_queue.end() &&
							position->sequence < item.sequence) {
							++position;
						}
						m_queue.splice(position, probe);
					}
				}
			} else {
				try {
					m_work(batch);
				} catch (...) {
					m_failure(batch, std::current_exception());
				}
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_active.erase(volume);
					m_owned -= batch.size();
				}
			}
			m_ready.notify_all();
		}
	}

	std::function<std::string(Job &)> m_resolve;
	std::function<void(Batch &)> m_work;
	Failure m_failure;
	// Runs under the scheduler lock: must perform no I/O or pool re-entry.
	std::function<bool(Job &)> m_discard;
	std::mutex m_mutex;
	std::condition_variable m_ready;
	std::list<Item> m_queue;
	std::map<std::string, std::string> m_volumes;
	std::set<std::string> m_active, m_resolving;
	std::array<std::thread, kWorkers> m_workers;
	size_t m_owned = 0, m_sequence = 0;
	bool m_stopping = false;
};
#endif
