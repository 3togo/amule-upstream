#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team. SPDX-License-Identifier: GPL-2.0-or-later
"""Independent SRP/AEAD peer for loopback integration tests.

Uses Python integers/hashlib and cryptography, independently of Crypto++.
EncryptedSocket presents decrypted EC frames to existing command fixtures.
"""
import hashlib
import hmac
import os
from pathlib import Path
import re
import struct
import sys
import zlib
try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305
except ImportError:
    print('SKIP: EC integration tests require Python cryptography')
    sys.exit(77)

C = {name: int(value, 0) for name, value in re.findall(
    r'^\s*(EC_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)\s*$',
    (Path(__file__).resolve().parents[2]/'src/libs/ec/abstracts/ECCodes.abstract').read_text(), re.M)}
N = int('''FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1 29024E08
8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD EF9519B3 CD3A431B
302B0A6D F25F1437 4FE1356D 6D51C245 E485B576 625E7EC6 F44C42E9
A637ED6B 0BFF5CB6 F406B7ED EE386BFB 5A899FA5 AE9F2411 7C4B1FE6
49286651 ECE45B3D C2007CB8 A163BF05 98DA4836 1C55D39A 69163FA8
FD24CF5F 83655D23 DCA3AD96 1C62F356 208552BB 9ED52907 7096966D
670C354E 4ABC9804 F1746C08 CA18217C 32905E46 2E36CE3B E39E772C
180E8603 9B2783A2 EC07A28F B5C55DF0 6F4C52C9 DE2BCBF6 95581718
3995497C EA956AE5 15D22618 98FA0510 15728E5A 8AAAC42D AD33170D
04507A33 A85521AB DF1CBA64 ECFB8504 58DBEF0A 8AEA7157 5D060C7D
B3970F85 A6E1E4C7 ABF5AE8C DB0933D7 1E8C94E0 4A25619D CEE3D226
1AD2EE6B F12FFA06 D98A0864 D8760273 3EC86A64 521F2B18 177B200C
BBE11757 7A615D6C 770988C0 BAD946E2 08E24FA0 74E5AB31 43DB5BFC
E0FD108E 4B82D120 A93AD2CA FFFFFFFF FFFFFFFF'''.replace(' ', '').replace('\n',''), 16)
def pad(x): return x.to_bytes(384, 'big')
def sha(x): return hashlib.sha256(x).digest()
k = int.from_bytes(sha(pad(N)+pad(5)), 'big')
def mac(key, data): return hmac.digest(key, data, 'sha256')
def derive(digest, salt):
    return int.from_bytes(hashlib.scrypt(digest, salt=salt, n=32768, r=8, p=1,
                                       dklen=32, maxmem=64*1024*1024), 'big')
def context(credential, salt, transcript, A, B):
    return (bytes([1,credential,15,8,1])+salt+struct.pack('!I',len(transcript))+
            transcript+pad(A)+pad(B))
def proofs(S, ctx):
    key = sha(pad(S))
    m1 = mac(key,b'ec-srp-client'+ctx)
    return m1,mac(key,b'ec-srp-server'+ctx+m1)
def exact(sock,n):
    out=b''
    while len(out)<n:
        b=sock.recv(n-len(out))
        if not b: raise EOFError('EC peer closed')
        out+=b
    return out
def tag(name,value=b'',kind=1):
    return struct.pack('!HBI',C[name]*2,kind,len(value))+value
def packet(op,tags):
    body=bytes([C[op]])+struct.pack('!H',len(tags))+b''.join(tags)
    return struct.pack('!II',0x20,len(body))+body
def read(sock):
    flags,length=struct.unpack('!II',exact(sock,8))
    body=exact(sock,length)
    if flags&1: body=zlib.decompress(body)
    offset=0
    def number(width):
        nonlocal offset
        if not flags&2:
            value=int.from_bytes(body[offset:offset+width],'big');offset+=width;return value
        first=body[offset];offset+=1
        if first<128: return first
        count=0; mask=128
        while first&mask: count+=1;mask>>=1
        assert 2<=count<=6
        value=first&((1<<(7-count))-1)
        for _ in range(count-1):
            part=body[offset];offset+=1;assert part&192==128
            value=(value<<6)|(part&63)
        return value
    op=number(1);count=number(2)
    out={}
    for _ in range(count):
        name,kind,size=number(2),number(1),number(4)
        assert not name&1, 'handshake fields must have no children'
        assert name>>1 not in out, 'duplicate handshake field'
        out[name>>1]=body[offset:offset+size];offset+=size
    return op,out
class EncryptedSocket:
    def __init__(self,sock,shared,cn,sn,transcript,cipher,server=False):
        self.sock=sock;self.pending=b'';self.tx=0;self.rx=0
        length=16 if cipher==1 else 32
        prk=mac(sn+cn,shared)
        info=b'aMule EC AEAD v1'+bytes([cipher])+transcript
        okm=b'';last=b'';counter=1
        while len(okm)<2*length+8:
            last=mac(prk,last+info+bytes([counter]));okm+=last;counter+=1
        material=[okm[:length],okm[length:2*length],okm[2*length:2*length+4],okm[2*length+4:2*length+8]]
        if server: material=[material[1],material[0],material[3],material[2]]
        cipher_class=AESGCM if cipher==1 else ChaCha20Poly1305
        self.enc=cipher_class(material[0]);self.dec=cipher_class(material[1])
        self.tp,self.rp=material[2:]
    def sendall(self,data):
        flags,length=struct.unpack('!II',data[:8]);assert len(data)==8+length
        body=self.enc.encrypt(self.tp+struct.pack('!Q',self.tx),data[8:],None);self.tx+=1
        self.sock.sendall(struct.pack('!II',flags|8,len(body))+body)
    def recv(self,n):
        if not self.pending:
            flags,length=struct.unpack('!II',exact(self.sock,8));assert flags&8,'clear packet after salt'
            body=self.dec.decrypt(self.rp+struct.pack('!Q',self.rx),exact(self.sock,length),None);self.rx+=1
            self.pending=struct.pack('!II',flags&~8,len(body))+body
        out=self.pending[:n];self.pending=self.pending[n:];return out
    def close(self): self.sock.close()
    def __getattr__(self,name): return getattr(self.sock,name)

def authenticate(sock,digest,credential=0,capabilities=(),before_proof=None):
    priv=X25519PrivateKey.generate();pub=priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw);cn=os.urandom(32)
    a=int.from_bytes(os.urandom(32),'big');A=pow(5,a,N)
    fields=[tag('EC_TAG_CLIENT_NAME',b'EC integration\0',6),tag('EC_TAG_CLIENT_VERSION',b'1\0',6),
            tag('EC_TAG_PROTOCOL_VERSION',struct.pack('!H',C['EC_CURRENT_PROTOCOL_VERSION']),3),
            tag('EC_TAG_CAN_AEAD',b'\x01'),tag('EC_TAG_AEAD_CLIENT_NONCE',cn),tag('EC_TAG_AEAD_CLIENT_PUBKEY',pub),
            tag('EC_TAG_SRP_A',pad(A)),tag('EC_TAG_SRP_CREDENTIAL',bytes([credential]))]
    sock.sendall(packet('EC_OP_AUTH_REQ',fields+list(capabilities)))
    op,tags=read(sock)
    assert op==C['EC_OP_AUTH_SALT'],(op,tags)
    assert tags[C['EC_TAG_SRP_PROFILE']]==bytes([1,15,8,1])
    B=int.from_bytes(tags[C['EC_TAG_SRP_B']],'big');assert 0<B<N
    salt=tags[C['EC_TAG_SRP_SALT']];sn=tags[C['EC_TAG_AEAD_SERVER_NONCE']];sp=tags[C['EC_TAG_AEAD_SERVER_PUBKEY']]
    cipher=int.from_bytes(tags[C['EC_TAG_AEAD_CIPHER']],'big');assert cipher==1
    transcript=b'\x01\x01'+bytes([cipher])+cn+sn+pub+sp
    encrypted=EncryptedSocket(sock,priv.exchange(X25519PublicKey.from_public_bytes(sp)),cn,sn,transcript,cipher)
    if before_proof: before_proof()
    x=derive(digest,salt);u=int.from_bytes(sha(pad(A)+pad(B)),'big');assert u
    S=pow((B-k*pow(5,x,N))%N,a+u*x,N)
    m1,m2=proofs(S,context(credential,salt,transcript,A,B))
    encrypted.sendall(packet('EC_OP_AUTH_PASSWD',[tag('EC_TAG_SRP_M1',m1)]))
    op,tags=read(encrypted)
    if op==C['EC_OP_AUTH_OK']: assert hmac.compare_digest(tags[C['EC_TAG_SRP_M2']],m2)
    return encrypted,op,tags

def accept(sock,capabilities=(),password=b'regression',server_proof=None):
    op,tags=read(sock);assert op==C['EC_OP_AUTH_REQ']
    A=int.from_bytes(tags[C['EC_TAG_SRP_A']],'big');assert 0<A<N
    credential=tags[C['EC_TAG_SRP_CREDENTIAL']][0]
    salt=os.urandom(16);v=pow(5,derive(hashlib.md5(password).digest(),salt),N)
    b=int.from_bytes(os.urandom(32),'big');B=(k*v+pow(5,b,N))%N
    cn=tags[C['EC_TAG_AEAD_CLIENT_NONCE']];cp=tags[C['EC_TAG_AEAD_CLIENT_PUBKEY']];offered=tags[C['EC_TAG_CAN_AEAD']]
    cipher=1 if 1 in offered else 2
    priv=X25519PrivateKey.generate();sp=priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw);sn=os.urandom(32)
    transcript=bytes([len(offered)])+offered+bytes([cipher])+cn+sn+cp+sp
    sock.sendall(packet('EC_OP_AUTH_SALT',[tag('EC_TAG_SRP_B',pad(B)),tag('EC_TAG_SRP_SALT',salt),
        tag('EC_TAG_SRP_PROFILE',bytes([1,15,8,1])),tag('EC_TAG_AEAD_CIPHER',bytes([cipher]),2),
        tag('EC_TAG_AEAD_SERVER_NONCE',sn),tag('EC_TAG_AEAD_SERVER_PUBKEY',sp)]))
    encrypted=EncryptedSocket(sock,priv.exchange(X25519PublicKey.from_public_bytes(cp)),cn,sn,transcript,cipher,True)
    u=int.from_bytes(sha(pad(A)+pad(B)),'big');assert u
    m1,m2=proofs(pow(A*pow(v,u,N)%N,b,N),context(credential,salt,transcript,A,B))
    op,tags=read(encrypted);assert op==C['EC_OP_AUTH_PASSWD']
    assert hmac.compare_digest(tags[C['EC_TAG_SRP_M1']],m1)
    evidence = [tag('EC_TAG_SRP_M2',m2)] if server_proof is None else server_proof(m2)
    encrypted.sendall(packet('EC_OP_AUTH_OK',evidence+[
        tag('EC_TAG_SERVER_VERSION',b'capability fixture\0',6)]+list(capabilities)))
    return encrypted
