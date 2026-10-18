#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team. SPDX-License-Identifier: GPL-2.0-or-later
"""Offline migration, production client logins and active split-channel MITM."""
import hashlib
import os
from pathlib import Path
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time

from AllSearchIntegrationTest import C, EC, tag, connect_daemon, free_ports, stop_daemon
from DaemonConfig import write_amule_conf
import ECWire as W

def wire_tags(fields):
    names = {v:k for k,v in C.items() if k.startswith('EC_TAG_')}
    result=[]
    for code,value in fields.items():
        name=names[code]
        kind=6 if name in ('EC_TAG_CLIENT_NAME','EC_TAG_CLIENT_VERSION') else (
            3 if name=='EC_TAG_PROTOCOL_VERSION' else (
                9 if name=='EC_TAG_VERSION_ID' else (2 if name=='EC_TAG_AEAD_CIPHER' else 1)))
        result.append(W.tag(name,value,kind))
    return result

class Relay(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            with socket.create_connection(('127.0.0.1',self.server.target),timeout=15) as upstream:
                op,req=W.read(self.request); assert op==C['EC_OP_AUTH_REQ']
                cp=req[C['EC_TAG_AEAD_CLIENT_PUBKEY']];cn=req[C['EC_TAG_AEAD_CLIENT_NONCE']]
                offered=req[C['EC_TAG_CAN_AEAD']]
                to_server=W.X25519PrivateKey.generate();ts_pub=to_server.public_key().public_bytes(W.serialization.Encoding.Raw,W.serialization.PublicFormat.Raw)
                req[C['EC_TAG_AEAD_CLIENT_PUBKEY']]=ts_pub
                upstream.sendall(W.packet('EC_OP_AUTH_REQ',wire_tags(req)))
                op,reply=W.read(upstream);assert op==C['EC_OP_AUTH_SALT']
                sp=reply[C['EC_TAG_AEAD_SERVER_PUBKEY']];sn=reply[C['EC_TAG_AEAD_SERVER_NONCE']]
                cipher=int.from_bytes(reply[C['EC_TAG_AEAD_CIPHER']],'big')
                to_client=W.X25519PrivateKey.generate();tc_pub=to_client.public_key().public_bytes(W.serialization.Encoding.Raw,W.serialization.PublicFormat.Raw)
                reply[C['EC_TAG_AEAD_SERVER_PUBKEY']]=tc_pub
                self.request.sendall(W.packet('EC_OP_AUTH_SALT',wire_tags(reply)))
                prefix=bytes([len(offered)])+offered+bytes([cipher])+cn+sn
                client_leg=W.EncryptedSocket(self.request,to_client.exchange(W.X25519PublicKey.from_public_bytes(cp)),cn,sn,prefix+cp+tc_pub,cipher,True)
                server_leg=W.EncryptedSocket(upstream,to_server.exchange(W.X25519PublicKey.from_public_bytes(sp)),cn,sn,prefix+ts_pub+sp,cipher)
                op,proof=W.read(client_leg);assert op==C['EC_OP_AUTH_PASSWD']
                self.server.observed=proof[C['EC_TAG_SRP_M1']]
                server_leg.sendall(W.packet('EC_OP_AUTH_PASSWD',wire_tags(proof)))
                op,result=W.read(server_leg)
                assert op==C['EC_OP_AUTH_FAIL'],(op,result)
                # AUTH_FAIL carries a string, not an SRP field.
                payload=[W.tag('EC_TAG_STRING',result[C['EC_TAG_STRING']],6)]
                client_leg.sendall(W.packet('EC_OP_AUTH_FAIL',payload))
        except BaseException as exc:
            self.server.error=exc

class FixtureServer(socketserver.ThreadingTCPServer):
    daemon_threads=True
    allow_reuse_address=True

class BadServerProof(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            def evidence(proof):
                if self.server.missing:
                    return []
                return [W.tag('EC_TAG_SRP_M2', bytes([proof[0] ^ 1]) + proof[1:])]
            encrypted = W.accept(self.request, server_proof=evidence)
            self.request.settimeout(3)
            try:
                self.server.extra = encrypted.recv(8)
            except (socket.timeout, EOFError):
                self.server.extra = b''
        except BaseException as exc:
            self.server.error = exc

class OldDaemon(socketserver.BaseRequestHandler):
    def handle(self):
        W.read(self.request)
        # Retired numeric salt tag; deliberately no SRP B.
        salt=struct.pack('!HBIQ',0x000b*2,5,8,123)
        if getattr(self.server,'bad_profile',False):
            fields=[W.tag('EC_TAG_SRP_B',W.pad(5)),W.tag('EC_TAG_SRP_SALT',bytes(16)),
                    W.tag('EC_TAG_SRP_PROFILE',bytes([1,31,8,1]))]
            self.request.sendall(W.packet('EC_OP_AUTH_SALT',fields))
        else:
            body=bytes([C['EC_OP_AUTH_SALT']])+struct.pack('!H',1)+salt
            self.request.sendall(struct.pack('!II',0x20,len(body))+body)
        self.request.settimeout(3)
        try: self.server.extra=self.request.recv(4096)
        except socket.timeout: self.server.extra=b''

def main(daemon,cmd,async_probe):
    with tempfile.TemporaryDirectory(prefix='amule-srp-') as scratch:
        root=Path(scratch);port,peer=free_ports(2)
        write_amule_conf(root,port,Port=peer,UDPEnable=0)
        env=dict(os.environ,HOME=scratch,XDG_CONFIG_HOME=str(root/'xdg'),LC_ALL='C.UTF-8')
        for restart in range(2):
            with (root/'console').open('w') as log:
                proc=subprocess.Popen([daemon,'-c',scratch,'--disable-fatal'],env=env,stdout=log,stderr=log)
                try:
                    ec=connect_daemon(proc,port,root,peer);ec.sock.close()
                    conf=(root/'amule.conf').read_text()
                    assert '\nECPassword=' not in conf
                    record=next(line for line in conf.splitlines() if line.startswith('ECPasswordVerifier='))
                    assert record.startswith('ECPasswordVerifier=v1:15:8:1:')
                    if restart: assert record==saved,'restart changed verifier/salt'
                    saved=record
                    out=subprocess.run([cmd,'-h','127.0.0.1','-p',str(port),'-P','regression','-c','status'],env=env,capture_output=True,text=True,timeout=20)
                    assert 'Succeeded!' in out.stdout+out.stderr,out
                    out=subprocess.run([async_probe,str(port)],env=env,capture_output=True,text=True,timeout=20)
                    assert out.returncode==0,out
                    print('PASS: migrated config, amulecmd and GUI async transport login, restart',restart)
                    if restart: continue
                    # A legacy client gets an explicit version error before any password proof.
                    with socket.create_connection(('127.0.0.1',port),timeout=10) as sock:
                        sock.sendall(W.packet('EC_OP_AUTH_REQ',[W.tag('EC_TAG_PROTOCOL_VERSION',struct.pack('!H',C['EC_CURRENT_PROTOCOL_VERSION']),3)]))
                        op,tags=W.read(sock)
                        assert op==C['EC_OP_AUTH_FAIL'] and b'3.2.0' in tags[C['EC_TAG_STRING']]
                    # Missing token follows a full SRP exchange and rejects a guess.
                    with socket.create_connection(('127.0.0.1',port),timeout=10) as sock:
                        encrypted,op,_=W.authenticate(sock,os.urandom(16),credential=1)
                        assert op==C['EC_OP_AUTH_FAIL']
                    with FixtureServer(('127.0.0.1',0),Relay) as relay:
                        relay.target=port;relay.error=None;relay.observed=None
                        threading.Thread(target=relay.serve_forever,daemon=True).start()
                        with socket.create_connection(relay.server_address,timeout=15) as sock:
                            _,op,_=W.authenticate(sock,hashlib.md5(b'regression').digest())
                            assert op==C['EC_OP_AUTH_FAIL']
                        relay.shutdown()
                        assert relay.error is None,relay.error
                        assert len(relay.observed)==32
                    print('PASS: old client, missing token, split X25519 MITM rejected')
                    # A malformed edit must report failure and leave the existing
                    # credential usable, rather than closing or rekeying the daemon.
                    editor = EC(port)
                    try:
                        malformed = tag(C['EC_TAG_PREFS_REMOTECTRL'], children=[
                            tag(C['EC_TAG_EXTERNALCONN_PASSWD'], b'not-a-digest\0', 6)])
                        assert editor.call(C['EC_OP_SET_PREFERENCES'], [malformed])[0] == C['EC_OP_FAILED']
                    finally:
                        editor.sock.close()
                    def change_password(old, new):
                        editor=EC(port,password=old)
                        try:
                            request=tag(C['EC_TAG_PREFS_REMOTECTRL'],children=[
                                tag(C['EC_TAG_EXTERNALCONN_PASSWD'],hashlib.md5(new.encode()).digest(),9)])
                            op,_=editor.call(C['EC_OP_SET_PREFERENCES'],[request])
                            assert op==C['EC_OP_NOOP']
                        finally: editor.sock.close()
                    with socket.create_connection(('127.0.0.1',port),timeout=15) as sock:
                        _,op,_=W.authenticate(sock,hashlib.md5(b'regression').digest(),
                            before_proof=lambda: change_password('regression','rotated'))
                        assert op==C['EC_OP_AUTH_FAIL'],'in-flight old password accepted after rotation'
                    for args in ([cmd, '-h', '127.0.0.1', '-p', str(port), '-P', 'rotated', '-c', 'status'],
                                 [async_probe, str(port), 'rotated']):
                        out = subprocess.run(args, env=env, capture_output=True, text=True, timeout=20)
                        assert 'Succeeded!' in out.stdout + out.stderr, out
                    rotated = next(line for line in (root/'amule.conf').read_text().splitlines()
                                   if line.startswith('ECPasswordVerifier='))
                    change_password('rotated', 'rotated')
                    again = next(line for line in (root/'amule.conf').read_text().splitlines()
                                 if line.startswith('ECPasswordVerifier='))
                    assert again != rotated, 'explicit password update reused the previous salt/verifier'
                    change_password('rotated','regression')
                    saved=next(line for line in (root/'amule.conf').read_text().splitlines()
                               if line.startswith('ECPasswordVerifier='))
                    print('PASS: remote password change invalidates pending old-password proof')

                finally:
                    stop_daemon(proc)
        with FixtureServer(('127.0.0.1',0),OldDaemon) as old:
            old.extra=None
            threading.Thread(target=old.serve_forever,daemon=True).start()
            out=subprocess.run([cmd,'-h','127.0.0.1','-p',str(old.server_address[1]),'-P','regression','-c','status'],env=env,capture_output=True,text=True,timeout=10)
            assert '3.2.0 or later' in out.stdout+out.stderr,out
            old.shutdown()
            assert old.extra==b'',old.extra
        with FixtureServer(('127.0.0.1',0),OldDaemon) as malicious:
            malicious.bad_profile=True;malicious.extra=None
            threading.Thread(target=malicious.serve_forever,daemon=True).start()
            start=time.monotonic()
            out=subprocess.run([cmd,'-h','127.0.0.1','-p',str(malicious.server_address[1]),'-P','regression','-c','status'],env=env,capture_output=True,text=True,timeout=5)
            assert 'unsupported profile' in out.stdout+out.stderr,out
            assert time.monotonic()-start<3,'client attempted unbounded KDF'
            malicious.shutdown();assert malicious.extra==b''
        print('PASS: unauthenticated excessive scrypt cost rejected without proof')
        for binary in (cmd, async_probe):
            for missing in (False, True):
                with FixtureServer(('127.0.0.1', 0), BadServerProof) as bad:
                    bad.missing = missing
                    bad.extra = None
                    bad.error = None
                    threading.Thread(target=bad.serve_forever, daemon=True).start()
                    args = ([cmd, '-h', '127.0.0.1', '-p', str(bad.server_address[1]),
                             '-P', 'regression', '-c', 'status'] if binary == cmd else
                            [async_probe, str(bad.server_address[1])])
                    out = subprocess.run(args, env=env, capture_output=True, text=True, timeout=20)
                    assert 'failed to prove' in out.stdout + out.stderr, out
                    assert 'Succeeded!' not in out.stdout + out.stderr, out
                    bad.shutdown()
                    assert bad.error is None, bad.error
                    assert bad.extra == b'', 'command sent before validating daemon proof'
        print('PASS: amulecmd and GUI transport reject missing/tampered server proofs before commands')
        out=subprocess.run([cmd,'--disable-ec-encryption'],env=env,capture_output=True,text=True,timeout=5)
        assert 'Unknown' in out.stdout+out.stderr or 'unknown' in out.stdout+out.stderr,out
        print('PASS: old daemon and removed encryption switch fail clearly')

if __name__=='__main__': main(*sys.argv[1:])
