#!/usr/bin/env bash
# Offline REST/SSE coverage for All searches discovered from the core.
# Unlike 19-search.sh, this phase needs no public eD2k/Kad connectivity.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "${1:-}" != "--fixture-ready" ]; then
	: "${AMULEAPI_BIN:?Run through run-all.sh or set AMULEAPI_BIN}"
	AMULED_BIN=${AMULED_BIN:-$(dirname "$AMULEAPI_BIN")/../amuled}
	exec python3 "$SCRIPT_DIR/../../tests/AllSearchApiSmoke.py" \
		"$AMULED_BIN" "$AMULEAPI_BIN" "$SCRIPT_DIR/45-all-search.sh"
fi

TMP=$(mktemp -d -t amuleapi_all_smoke.XXXXXX)
SSE_PID=
cleanup() {
	if [ -n "$SSE_PID" ]; then
		kill "$SSE_PID" 2>/dev/null || true
		wait "$SSE_PID" 2>/dev/null || true
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
TOKEN=$(curl -fsS --max-time 10 -X POST -H 'Content-Type: application/json' \
	-d '{"password":"adminpass"}' "$API/auth/login?include_token=true" | jq -er .token)
AUTH="Authorization: Bearer $TOKEN"
curl -fsS -N --max-time 20 -D "$TMP/headers" -H "$AUTH" \
	"$API/events" >"$TMP/events" 2>"$TMP/curl-error" &
SSE_PID=$!
# Wait for the subscription before creating the session's search-results slot.
for _ in $(seq 1 100); do
	if grep -qi 'content-type: text/event-stream' "$TMP/headers"; then break; fi
	kill -0 "$SSE_PID" 2>/dev/null || fail 'SSE connection exited'
	sleep 0.1
done
grep -qi 'content-type: text/event-stream' "$TMP/headers" || fail 'SSE did not subscribe'
curl -fsS --max-time 10 -H "$AUTH" "$API/search" >"$TMP/searches"
jq -e --argjson sid "$ALL_SID" \
	'any(.searches[]; .search_id == $sid and .type == "all")' "$TMP/searches" >/dev/null \
	|| fail 'GET /search did not report type all'
echo 'PASS: GET /search reports type all'
curl -fsS --max-time 10 -H "$AUTH" "$API/search/$ALL_SID/results" >"$TMP/results"
echo 'PASS: GET All search results'
wait "$SSE_PID" || true
SSE_PID=
awk '/^event: / { progress = ($0 == "event: search_progress") }
	progress && /^data: / { sub(/^data: /, ""); print }' "$TMP/events" \
	| jq -es --argjson sid "$ALL_SID" \
	'any(.[]; .search_id == $sid and .type == "all")' >/dev/null \
	|| fail 'SSE search_progress did not report type all'
echo 'PASS: SSE search_progress reports type all'
STATUS=$(curl -sS --max-time 10 -o "$TMP/delete" -w '%{http_code}' \
	-X DELETE -H "$AUTH" "$API/search/$ALL_SID")
[ "$STATUS" = 204 ] || fail "DELETE All search returned $STATUS"
echo 'PASS: DELETE All search (HTTP 204)'
echo 'OK: 4/4 passed (no skips)'
