import configparser, ctypes, json, os, socket, subprocess, sys, tempfile, time
from pathlib import Path
source=Path(sys.argv[1]).resolve()
sys.path.insert(0, str(source/'unittests/tests'))
from DaemonConfig import write_amule_conf, daemon_env
from AllSearchIntegrationTest import EC, C, string
out=Path(sys.argv[2]).resolve();out.mkdir(exist_ok=True)
binary=str(source/'build/src/amule')
x=subprocess.Popen(['Xvfb',':187','-screen','0','1024x768x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1)
results=[]
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def run(label,setting,reuse=None,capture=False):
 root=reuse or Path(tempfile.mkdtemp(prefix='amule-flat-'+label+'-'))
 ecport=port()
 if reuse:
  cfg=configparser.ConfigParser();cfg.optionxform=str;cfg.read(root/'amule.conf');cfg['ExternalConnect']['ECPort']=str(ecport)
  with (root/'amule.conf').open('w') as f:cfg.write(f)
 else:
  (root/'Incoming').mkdir();(root/'Temp').mkdir();(root/'lastversion').write_text('GIT\n')
  write_amule_conf(root,ecport,Port=port(),UDPEnable=0,ConfirmExit=0,**{'3DDepth':setting},sections={'GUI':{'StartupTab':2}})
 env=daemon_env(root,DISPLAY=':187',LANG='en_US.UTF-8')
 with (root/'console.log').open('w') as log:
  p=subprocess.Popen([binary,'-c',str(root),'--disable-fatal'],env=env,stdout=log,stderr=log)
  try:
   deadline=time.monotonic()+40
   while True:
    assert p.poll() is None,(root/'console.log').read_text()
    try: ec=EC(ecport);break
    except (ConnectionRefusedError,EOFError,OSError):
     assert time.monotonic()<deadline,(root/'console.log').read_text()
     time.sleep(.25)
   if capture and not reuse:
    response=ec.call(C['EC_OP_ADD_LINK'],[string(C['EC_TAG_STRING'],'ed2k://|file|example-download.bin|104857600|0123456789ABCDEF0123456789ABCDEF|/')])
    assert response[0]==C['EC_OP_NOOP'],response
   ec.sock.close()
   time.sleep(2)
   if capture:
    subprocess.run(['import','-window','root',str(out/(label+'.png'))],env=env,check=True)
   p.terminate();p.wait(timeout=20)
  finally:
   if p.poll() is None:p.kill();p.wait()
 cfg=configparser.ConfigParser();cfg.read(root/'amule.conf')
 actual=cfg.getint('eMule','3DDepth');expected=0 if setting is None else setting
 assert actual==expected,(label,actual,expected)
 results.append({'case':label,'saved_depth':actual,'profile':str(root)})
 return root
try:
 rounded=run('rounded',10,capture=True)
 run('rounded-restart',10,reuse=rounded)
 flat=run('flat',None,capture=True)
 run('flat-restart',0,reuse=flat)
 for value in (0,1,5):run('saved-'+str(value),value)
 (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(json.dumps(results,indent=2))
finally:
 x.terminate();x.wait()
