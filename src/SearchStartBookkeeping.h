#ifndef SEARCHSTARTBOOKKEEPING_H
#define SEARCHSTARTBOOKKEEPING_H

#include "SearchList.h"

#include <ctime>

// Hold the proposed scalar search state until startup has completed.
class CSearchStartBookkeeping
{
public:
	CSearchStartBookkeeping(SearchType &currentType,
		time_t &currentStart,
		wxString &currentResultType,
		SearchType requestedType,
		const wxString &requestedResultType,
		bool preserveType)
	: m_currentType(currentType)
	, m_currentStart(currentStart)
	, m_currentResultType(currentResultType)
	, m_requestedType(requestedType)
	, m_requestedResultType(requestedResultType)
	, m_preserveType(preserveType)
	{
	}

	void Commit(time_t start)
	{
		if (!m_preserveType) {
			m_currentType = m_requestedType;
		}
		m_currentStart = start;
		m_currentResultType = m_requestedResultType;
	}

private:
	SearchType &m_currentType;
	time_t &m_currentStart;
	wxString &m_currentResultType;
	SearchType m_requestedType;
	wxString m_requestedResultType;
	bool m_preserveType;
};

#endif
