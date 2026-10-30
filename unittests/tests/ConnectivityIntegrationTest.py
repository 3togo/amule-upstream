#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify connectivity snapshots through a real, isolated, offline daemon."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from AllSearchIntegrationTest import (C, EC, connect_daemon, retry_port_collisions,
                                      stop_daemon, tag)
from LogfilePathIntegrationTest import daemon_env, write_config


class SnapshotClient(EC):
    def call(self, opcode, tags=()):
        result, reply = super().call(opcode, tags)
        if result == C['EC_OP_AUTH_OK']:
            assert C['EC_TAG_CAN_CONNECTIVITY'] in reply, 'capability not advertised'
        return result, reply


def run(binary):
    with tempfile.TemporaryDirectory(prefix='amule-connectivity-') as name:
        root = Path(name)
        ec_port, peer_port = write_config(root)
        # Enable eD2k without connecting anywhere. Kad and client UDP stay disabled.
        config = root / 'amule.conf'
        config.write_text(config.read_text().replace('ConnectToED2K=0', 'ConnectToED2K=1'))
        with (root / 'console.log').open('w') as console:
            proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'],
                                    stdout=console, stderr=console, env=daemon_env(root))
            ec = None
            try:
                initial = connect_daemon(proc, ec_port, root, peer_port)
                initial.sock.close()
                ec = SnapshotClient(ec_port)
                for request in ((), (tag(C['EC_TAG_CONNECTIVITY_RECHECK']),)):
                    opcode, fields = ec.call(C['EC_OP_GET_CONNECTIVITY'], request)
                    assert opcode == C['EC_OP_GET_CONNECTIVITY'], opcode
                    def value(name):
                        return fields[C['EC_TAG_CONNECTIVITY_' + name]][0]
                    assert value('TCP_PORT') == peer_port, fields
                    assert value('ADDRESS') == b'127.0.0.1\0', fields
                    assert value('FLAGS') & 1, 'core listener was not observed'
                    assert not value('FLAGS') & 4, 'disabled UDP reported enabled'
                    assert value('ED2K') == 1, 'disconnected eD2k reported reachable'
                    assert value('KAD_TCP') == value('KAD_UDP') == 0, fields
                    assert abs(value('READ_AT') - time.time()) < 30, fields
                    assert C['EC_TAG_CONNECTIVITY_MAPPING'] not in fields, fields
                print('Connectivity EC snapshot and offline recheck passed')
            finally:
                if ec is not None:
                    ec.sock.close()
                stop_daemon(proc)


if __name__ == '__main__':
    retry_port_collisions(run, str(Path(sys.argv[1]).resolve()))
