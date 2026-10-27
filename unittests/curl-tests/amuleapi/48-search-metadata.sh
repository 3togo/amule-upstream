#!/usr/bin/env bash
# Offline metadata search coverage through the real API and daemon.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "${1:-}" != "--fixture-ready" ]; then
	: "${AMULEAPI_BIN:?Set AMULEAPI_BIN}"
	AMULED_BIN=${AMULED_BIN:-$(dirname "$AMULEAPI_BIN")/../amuled}
	exec python3 "$SCRIPT_DIR/../../tests/AllSearchApiSmoke.py" \
		"$AMULED_BIN" "$AMULEAPI_BIN" "$SCRIPT_DIR/48-search-metadata.sh"
fi
TOKEN=$(curl -fsS --max-time 10 -X POST -H 'Content-Type: application/json' \
	-d '{"password":"adminpass"}' "$API/auth/login?include_token=true" | jq -er .token)
export TOKEN
python3 - <<'PY'
import json
import os
import urllib.error
import urllib.request

base = os.environ['API']
def request(path, body=None, method=None):
    req = urllib.request.Request(base + path,
        None if body is None else body if isinstance(body, bytes) else json.dumps(body).encode(),
        {'Authorization': 'Bearer ' + os.environ['TOKEN'], 'Content-Type': 'application/json'},
        method=method)
    try:
        response = urllib.request.urlopen(req, timeout=10)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)
    with response:
        payload = response.read()
        return response.status, json.loads(payload) if payload else None

assert request('/networks/connect', {'network': 'kad'})[0] < 300
filters = {'min_bitrate_kbps': 320, 'min_length': '1:02:03', 'codec': 'FLAC',
           'title': '曲 AND OR NOT', 'album': 'Test Album', 'artist': 'Test Artist'}
expected = dict(filters, min_length=3723)
status, created = request('/search', {'query': 'offlinemetadataregression', 'type': 'kad', **filters})
assert status == 202, (status, created)
sid = created['search_id']
assert all(created.get(k) == v for k, v in expected.items()), created
status, listing = request('/search')
assert status == 200
row = next(row for row in listing['searches'] if row['search_id'] == sid)
assert all(row.get(k) == v for k, v in expected.items()), row
print('PASS: all metadata filters survive REST -> EC -> core -> EC -> REST')

invalid = [('min_bitrate_kbps', v) for v in (-1, 0.5, 1000001, 2**64, '128', True, None)]
invalid += [('min_length', v) for v in (-1, 0.5, 86401, 2**64, '', '1:02', '1:2:03',
                                           '1:60:00', '24:00:01', '99999999999999999999', True, None)]
invalid += [(key, v) for key in ('codec', 'title', 'album', 'artist') for v in (123, None, True, 'a\0b')]
for key, value in invalid:
    status, result = request('/search', {'query': 'offlinemetadatainvalid', 'type': 'kad', key: value})
    assert status == 400 and result['error']['code'] == 'bad_request', (key, value, status, result)
assert next(row for row in request('/search')[1]['searches'] if row['search_id'] == sid)['title'] == expected['title']
status, result = request('/search', b'{"query":"offlinemetadatainvalid","type":"kad","codec":"\xff"}')
assert status == 400 and result['error']['code'] == 'bad_request', (status, result)
assert not any(row['query'] == 'offlinemetadatainvalid' for row in request('/search')[1]['searches'])
print('PASS: invalid input creates no search and preserves the running search')
assert request('/search/' + str(sid), method='DELETE')[0] == 204

for index, duration in enumerate((0, 86400, '86400', '24:00:00')):
    status, result = request('/search', {'query': 'offlinemetadataboundary' + str(index),
        'type': 'kad', 'min_length': duration, 'min_bitrate_kbps': 1000000})
    assert status == 202, (status, result)
    assert result['min_length'] == (0 if duration == 0 else 86400)
    assert request('/search/' + str(result['search_id']), method='DELETE')[0] == 204
print('PASS: inclusive limits and both duration formats')
PY
