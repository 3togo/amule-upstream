// This file is part of the aMule Project.
// Licensed under the GNU General Public License, version 2 or later.
#ifndef KADAICHVOTES_H
#define KADAICHVOTES_H

#include <map>
#include "SHAHashSet.h"

// Transient search-result evidence, never persisted as trusted metadata.
class CKadAICHVotes
{
public:
	void Add(uint32_t responder, const CAICHHash &root)
	{
		// Retain the first root per responder and bound memory per result.
		if (responder != 0 && m_votes.size() < 64) {
			m_votes.emplace(responder, root);
		}
	}

	void Merge(const CKadAICHVotes &other)
	{
		for (const auto &vote : other.m_votes) {
			Add(vote.first, vote.second);
		}
	}

	const std::map<uint32_t, CAICHHash> &Get() const { return m_votes; }

private:
	std::map<uint32_t, CAICHHash> m_votes;
};

#endif
