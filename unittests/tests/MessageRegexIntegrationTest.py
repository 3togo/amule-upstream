#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise message regex over EC and real offline peers, with isolated config."""
import os
import json
import configparser
import time
import urllib.request
import urllib.error
from pathlib import Path
import subprocess
import sys
import tempfile
from AllSearchIntegrationTest import (C, connect_daemon, free_ports, integer,
    retry_port_collisions, stop_daemon, string, tag)
from ChatSpamFilterIntegrationTest import sender, latest, preferences
from DaemonConfig import write_amule_conf


def run(binary, api_binary=None):
    with tempfile.TemporaryDirectory(prefix='amule-message-regex-') as temporary:
        root = Path(temporary)
        ports = free_ports(4, udp_indices=(2,), peer_index=1)
        write_amule_conf(root, ports[0], Port=ports[1], UDPPort=ports[2], UDPEnable=0,
            FilterLanIPs=0, FilterMessages=1, FilterAllMessages=0,
            MessagesFromFriendsOnly=0, MessageFromValidSourcesOnly=0,
            FilterWordMessages=1, AdvancedSpamFilter=0, MessageUseCaptchas=0)
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root/'xdg'), LC_ALL='C.UTF-8')
        env.pop('LD_PRELOAD', None)
        env.pop('DISPLAY', None)
        with (root/'console.log').open('w') as log:
            proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'],
                env=env, stdout=log, stderr=log)
        ec = None
        count = 0
        def update(children, expected=None):
            result = ec.call(C['EC_OP_SET_PREFERENCES'], [tag(C['EC_TAG_PREFS_MESSAGEFILTER'], children=children),
                integer(C['EC_TAG_DETAIL_LEVEL'], C['EC_DETAIL_FULL'])])
            assert result[0] == (C['EC_OP_NOOP'] if expected is None else expected), result
            return result
        def check(text, dropped):
            nonlocal count
            count += 1
            with sender(ports[1], f'regex-peer-{count}') as (peer_hash, message):
                message(text)
                assert latest(ec, peer_hash) == (None if dropped else text), (text, dropped)
        try:
            ec = connect_daemon(proc, ports[0], root, ports[1], None)
            assert preferences(ec)[C['EC_TAG_MSGFILTER_REGEX_ENABLED']][0] == 0
            update([integer(C['EC_TAG_MSGFILTER_REGEX_ENABLED'], 1),
                    string(C['EC_TAG_MSGFILTER_REGEX_PATTERN'], 'dog.*')])
            check('hotDOG', True)
            check('cat', False)
            update([string(C['EC_TAG_MSGFILTER_REGEX_PATTERN'], 'spam$')])
            check('this is SPAM', True)
            check('spam alert', False)
            # Reject the entire packet before it changes even unrelated preferences.
            before = preferences(ec)
            update([string(C['EC_TAG_MSGFILTER_REGEX_PATTERN'], '('),
                    integer(C['EC_TAG_MSGFILTER_ENABLED'], 0),
                    integer(C['EC_TAG_MSGFILTER_FILTER_COMMENTS'], 1)], C['EC_OP_FAILED'])
            assert preferences(ec) == before
            check('still spam', True)
            update([string(C['EC_TAG_MSGFILTER_REGEX_PATTERN'], '[0-9]{2,4}')])
            check('number 123', True)
            check('one 7', False)
            # Old clients omit the new tags: saving a legacy field must preserve regex.
            for detail in (C['EC_DETAIL_FULL'], C['EC_DETAIL_CMD']):
                group = tag(C['EC_TAG_PREFS_MESSAGEFILTER'], children=[
                    string(C['EC_TAG_MSGFILTER_KEYWORDS'], '*')])
                assert ec.call(C['EC_OP_SET_PREFERENCES'], [group,
                    integer(C['EC_TAG_DETAIL_LEVEL'], detail)])[0] == C['EC_OP_NOOP']
                saved = preferences(ec)
                assert saved[C['EC_TAG_MSGFILTER_REGEX_ENABLED']][0] == 1
                assert saved[C['EC_TAG_MSGFILTER_REGEX_PATTERN']][0].rstrip(b'\0') == b'[0-9]{2,4}'
            # CMD's legacy presence flags clear omitted master/content toggles;
            # restore them explicitly before checking the retained regex.
            update([integer(C['EC_TAG_MSGFILTER_ENABLED'], 1),
                    integer(C['EC_TAG_MSGFILTER_BY_KEYWORD'], 1)])
            check('ordinary text', False)
            update([integer(C['EC_TAG_MSGFILTER_REGEX_ENABLED'], 0)])
            check('legacy star blocks everything', True)
            # Both stored expressions survive a daemon restart, preserving the selected mode.
            update([integer(C['EC_TAG_MSGFILTER_REGEX_ENABLED'], 1),
                    string(C['EC_TAG_MSGFILTER_REGEX_PATTERN'], 'spam$')])
            if api_binary:
                api_config = root/'api'
                api_config.mkdir()
                command = [api_binary, '--config-dir='+str(api_config)]
                with (root/'api.log').open('w') as log:
                    subprocess.run(command+['--set-admin-pass-stdin'], input=b'regression\n',
                        check=True, env=env, stdout=log, stderr=log, timeout=10)
                    (api_config/'amuleapi.conf').write_text('[EC]\nPassword=regression\n')
                    api = subprocess.Popen(command+['--host=127.0.0.1', '--port='+str(ports[0]),
                        '--http-port='+str(ports[3]), '--disable-ec-encryption'],
                        env=env, stdout=log, stderr=log)
                opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
                token = None
                def request(path, data=None, method=None):
                    headers = {'Content-Type': 'application/json'}
                    if token: headers['Authorization'] = 'Bearer '+token
                    req = urllib.request.Request(f'http://127.0.0.1:{ports[3]}/api/v1'+path,
                        data=None if data is None else json.dumps(data).encode(),
                        headers=headers, method=method)
                    with opener.open(req, timeout=5) as response:
                        return json.load(response)
                try:
                    deadline = time.monotonic()+30
                    while True:
                        assert api.poll() is None, (root/'api.log').read_text()
                        try:
                            if request('/health').get('ec_connected'): break
                        except OSError: pass
                        assert time.monotonic() < deadline, (root/'api.log').read_text()
                        time.sleep(0.1)
                    token = request('/auth/login?include_token=true', {'password': 'regression'})['token']
                    response = request('/preferences', {'message_filter': {'use_regex': False}}, 'PATCH')
                    assert response['message_filter']['regex_supported']
                    assert not response['message_filter']['use_regex']
                    try:
                        request('/preferences', {'message_filter': {'use_regex': True, 'regex_pattern': '('}}, 'PATCH')
                        raise AssertionError('invalid REST regex was accepted')
                    except urllib.error.HTTPError as error:
                        assert error.code == 400, error.code
                        assert 'regular expression' in error.read().decode()
                    assert preferences(ec)[C['EC_TAG_MSGFILTER_REGEX_ENABLED']][0] == 0
                    response = request('/preferences', {'message_filter': {'use_regex': True, 'regex_pattern': 'spam$'}}, 'PATCH')
                    assert response['message_filter']['use_regex']
                    assert response['message_filter']['regex_pattern'] == 'spam$'
                    check('REST spam', True)
                    check('REST ordinary text', False)
                finally:
                    api.terminate()
                    api.wait(timeout=15)
        finally:
            if ec: ec.sock.close()
            stop_daemon(proc)
        with (root/'restart.log').open('w') as log:
            proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'],
                env=env, stdout=log, stderr=log)
        ec = None
        try:
            ec = connect_daemon(proc, ports[0], root, ports[1], None)
            saved = preferences(ec)
            assert saved[C['EC_TAG_MSGFILTER_REGEX_ENABLED']][0] == 1
            assert saved[C['EC_TAG_MSGFILTER_REGEX_PATTERN']][0].rstrip(b'\0') == b'spam$'
            check('restart spam', True)
            check('restart hello', False)
        finally:
            if ec: ec.sock.close()
            stop_daemon(proc)
        # A malformed on-disk regex disables only that rule, never falling back
        # to the independently saved word list (which is '*' in this fixture).
        config = configparser.RawConfigParser()
        config.optionxform = str
        config.read(root/'amule.conf')
        config['eMule']['MessageFilterRegex'] = '('
        with (root/'amule.conf').open('w') as handle:
            config.write(handle)
        with (root/'invalid-startup.log').open('w') as log:
            proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'],
                env=env, stdout=log, stderr=log)
        ec = None
        try:
            ec = connect_daemon(proc, ports[0], root, ports[1], None)
            assert preferences(ec)[C['EC_TAG_MSGFILTER_REGEX_ENABLED']][0] == 1
            check('invalid regex must not activate the saved star word list', False)
            assert 'Invalid message regular expression' in (root/'logfile').read_text(errors='replace')
        finally:
            if ec: ec.sock.close()
            stop_daemon(proc)

if __name__ == '__main__':
    retry_port_collisions(run, sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
