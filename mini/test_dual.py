"""Exercise two real host services, independent boot edits, and updater exclusion."""
import os, signal, subprocess, sys, tempfile, time
from pathlib import Path

root=Path(sys.argv[1]).resolve()
mini_bin=Path(sys.argv[2]).resolve();mk2_bin=Path(sys.argv[3]).resolve()
apps=[root/'Scripts/.config/buttongoblin-mini',root/'Scripts/.config/buttongoblin']

def wait_for(check,seconds=6):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        if check():return
        time.sleep(.02)
    raise AssertionError('Timed out waiting for service/test job')

with tempfile.TemporaryDirectory(prefix='dual-deck-test-') as directory:
    temp=Path(directory)
    # Both actual supervisors and workers must live concurrently; stopping one
    # must leave the other alive. No USB device is required for waiting mode.
    services=[]
    try:
        for index,(app,binary) in enumerate(zip(apps,[mini_bin,mk2_bin])):
            runtime=temp/f'service{index}'
            p=subprocess.Popen([str(binary),'--config',str(app/'buttons.conf'),'--assets',str(app/'icons'),'--runtime',str(runtime)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
            services.append(p)
            def ready(runtime=runtime,p=p):
                assert p.poll() is None
                path=Path(str(runtime)+'.worker_status')
                return path.exists() and 'usb_connected=0' in path.read_text()
            wait_for(ready)
        assert all(p.poll() is None for p in services)
        services[0].terminate();assert services[0].wait(timeout=6)==0
        assert services[1].poll() is None
    finally:
        for p in services:
            if p.poll() is None:p.terminate()
            try:p.wait(timeout=6)
            except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
    print('PASS: simultaneous Mini/MK.2 supervisors and independent shutdown')

    # Run both boot installers in a fake FAT tree with real validation binaries.
    # The test never edits the machine startup file or launches a real service.
    fake=temp/'fat';(fake/'linux').mkdir(parents=True)
    hook=temp/'S99user';hook.touch()
    startup=fake/'linux/user-startup.sh';startup.write_text('#!/bin/sh\necho keep-me\nexit 0\n')
    boot_scripts=[]
    for app,binary,prefix in zip(apps,[mini_bin,mk2_bin],['ButtonGoblin_Mini_','ButtonGoblin_']):
        target=fake/'Scripts/.config'/app.name;(target/'extras').mkdir(parents=True)
        (target/'buttongoblin-mister').write_text('#!/bin/sh\nexit 0\n');(target/'buttongoblin-mister').chmod(0o755)
        for operation in ['Start','Stop']:(target/'extras'/f'{prefix}{operation}.sh').write_text('#!/bin/sh\nexit 0\n')
        paths=[]
        for operation in ['Enable','Disable']:
            src=app/'extras'/f'{prefix}{operation}_Boot.sh'
            path=target/'extras'/src.name
            path.write_text(src.read_text().replace('/media/fat',str(fake)).replace('/etc/init.d/S99user',str(hook)))
            paths.append(path)
        boot_scripts.append(paths)
    env=dict(os.environ,BUTTONGOBLIN_BOOT_CONFIG_ONLY='1')
    for enable,_ in boot_scripts:subprocess.run(['sh',str(enable)],env=env,check=True,capture_output=True)
    content=startup.read_text();assert content.count('# BUTTONGOBLIN-MINI BEGIN')==1 and content.count('# BUTTONGOBLIN-MK2 BEGIN')==1 and 'echo keep-me' in content
    # Repeated enables must not accumulate boot blocks.
    subprocess.run(['sh',str(boot_scripts[0][0])],env=env,check=True,capture_output=True)
    assert startup.read_text().count('# BUTTONGOBLIN-MINI BEGIN')==1
    subprocess.run(['sh',str(boot_scripts[0][1])],check=True,capture_output=True)
    assert 'BUTTONGOBLIN-MINI' not in startup.read_text() and '# BUTTONGOBLIN-MK2 BEGIN' in startup.read_text()
    subprocess.run(['sh',str(boot_scripts[1][1])],check=True,capture_output=True)
    assert startup.read_text()=='#!/bin/sh\necho keep-me\nexit 0\n'
    print('PASS: independent boot blocks, repeated enables and preservation of unrelated startup commands')

    # A failure from the first deck must not skip the second deck's operation.
    for operation in ['Enable','Disable']:
        marker=fake/'mini-attempted'
        (fake/'Scripts'/f'ButtonGoblin_{operation}.sh').write_text('#!/bin/sh\nexit 1\n')
        (fake/'Scripts'/f'ButtonGoblin_Mini_{operation}.sh').write_text('#!/bin/sh\ntouch "'+str(marker)+'"\n')
        wrapper=temp/f'both-{operation}.sh'
        wrapper.write_text((root/'Scripts'/f'ButtonGoblin_Both_{operation}.sh').read_text().replace('/media/fat',str(fake)))
        result=subprocess.run(['sh',str(wrapper)],capture_output=True)
        assert result.returncode==1 and marker.exists()
        marker.unlink()
    print('PASS: both-deck controls attempt each service and propagate failures')

    # Compile small drivers around each exact bundled native source. They run a
    # harmless temporary job through the real guardian and shared updater lock.
    drivers=[]
    for index,app in enumerate(apps):
        source=temp/f'driver{index}.c';binary=temp/f'driver{index}'
        source.write_text('''#define main launcher_main
#include "'''+str(app/'source/buttongoblin.c')+'''"
#undef main
int main(int argc,char **argv){
  if(argc!=2)return 2;
  if(textset(base,sizeof base,argv[1]))return 2;
  char label[64];snprintf(label,sizeof label,"driver-%ld",(long)getpid());
  if(join(runtime,sizeof runtime,argv[1],label))return 2;
  cfg=page_configs[0]=newconfig();active_page=0;
  cfg->keys[0].action=UPDATE_ALL;if(join(cfg->keys[0].target,PATHCAP,argv[1],"job.sh"))return 2;
  int rc=start_script(0,-1,-1);
  if(rc){freeconfig(cfg);return rc==-2?75:2;}
  puts("READY");fflush(stdout);
  while(script_pid>0){reap_page_script(nowtime(),0);usleep(1000);}
  rc=completed_key==0?0:3;freeconfig(cfg);return rc;
}
''')
        subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
        drivers.append(binary)
    job=temp/'job.sh';release=temp/'release';runs=temp/'runs'
    job.write_text('#!/bin/sh\nprintf "run\\n" >> "'+str(runs)+'"\nwhile [ ! -f "'+str(release)+'" ]; do sleep .02; done\n')
    for first,second in [(drivers[0],drivers[1]),(drivers[1],drivers[0])]:
        p=subprocess.Popen([str(first),str(temp)],stdout=subprocess.PIPE,text=True,start_new_session=True)
        try:
            assert p.stdout.readline().strip()=='READY'
            wait_for(lambda:runs.exists())
            assert subprocess.run([str(second),str(temp)],capture_output=True,timeout=5).returncode==75
            release.touch();assert p.wait(timeout=6)==0
        finally:
            if p.poll() is None:os.killpg(p.pid,signal.SIGKILL);p.wait()
        release.unlink()
    assert runs.read_text().splitlines()==['run','run']
    print('PASS: cross-device Update All exclusion and lock release, in both directions')

assert not (root/'Scripts/.config/buttongoblin-mini/buttons.conf').samefile(root/'Scripts/.config/buttongoblin/buttons.conf')
assert 'buttongoblin_mini' in (apps[0]/'extras/ButtonGoblin_Mini_Start.sh').read_text()
assert 'buttongoblin_mini' not in (apps[1]/'extras/ButtonGoblin_Start.sh').read_text()
print('PASS: separate configuration trees, service names and runtime paths')
