// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef NETWORKPREFERENCES_H
#define NETWORKPREFERENCES_H

// Reconcile a checkbox with core updates without discarding an uncommitted user edit.
class CNetworkPreferenceEdit
{
public:
	void Reset(bool enabled) { m_synced = enabled; }
	bool Merge(bool displayed, bool enabled)
	{
		const bool result = displayed == m_synced ? enabled : displayed;
		m_synced = enabled;
		return result;
	}

private:
	bool m_synced = false;
};

#endif // NETWORKPREFERENCES_H
