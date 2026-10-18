"""End-to-end Mini package checks; never opens a USB device or runs Update All."""
import io, json, struct, subprocess, sys, tempfile
from pathlib import Path
from PIL import Image

root=Path(sys.argv[1]).resolve();binary=Path(sys.argv[2]).resolve()
app=root/'Scripts/.config/streamdeck-mini';icons=app/'icons'
meta=json.loads((root/'MINI_LAYOUT.json').read_text())
assert meta['update_all']=={'page':1,'physical_key':4,'target':'Scripts/update_all.sh'}
assert len(meta['pages'])==6 and all(len(p)==6 for p in meta['pages'])
assert not (root/'Scripts/.config/streamdeck').exists()
assert not (root/'Scripts/StreamDeck_Enable.sh').exists()
assert (root/'Scripts/StreamDeck_Mini_Enable.sh').exists()
assert (root/'Scripts/StreamDeck_Mini_Disable.sh').exists()
assert f'#define VERSION "Mini {meta["version"]}"' in (app/'source/streamdeck.c').read_text()
assert subprocess.check_output([str(binary),'--version'],text=True).strip()==f'Mini {meta["version"]}'

def fields(path):
    return dict(line.split('=',1) for line in path.read_text().splitlines() if line and not line.startswith('#'))
def run(config='buttons.conf',*args):
    return subprocess.run([str(binary),'--config',str(app/config),'--assets',str(icons),*args],capture_output=True,text=True)

for name in meta['page_files']:
    p=app/name;conf=fields(p);assert conf['key.5.action']=='next'
    assert all(not k.startswith('key.6.') for k in conf)
    assert run(name,'--validate').returncode==0,name
    unique=set()
    for k in range(6):
        assert f'key.{k}.request_image' in conf
        for state in ['image','request_image','error_image']:
            path=icons/conf[f'key.{k}.{state}'];assert path.suffix=='.sma'
            data=path.read_bytes();assert data[:4]==b'SMAN';version,count=struct.unpack_from('<HH',data,4)
            assert version==1 and 1<=count<=32;assert len(data)<1048576
            pos=8
            for frame in range(count):
                duration,size=struct.unpack_from('<II',data,pos);pos+=8
                assert duration>=350 and size==19254
                bmp=data[pos:pos+size];assert bmp[:2]==b'BM'
                image=Image.open(io.BytesIO(bmp));assert image.size==(80,80) and image.mode=='RGB';image.load()
                pos+=size
            assert pos==len(data);unique.add(path)
    if conf.get('key.3.action')=='update_all':unique.add(icons/conf['key.3.success_image'])
    assert sum(p.stat().st_size for p in unique)<4194304

confirm=fields(app/'buttons-confirm.conf')
assert [confirm[f'key.{i}.action'] for i in range(6)]==['y','n','none','back','none','next']
assert len(meta['page_files'])==6 and len(set(meta['page_files']))==6
home=fields(app/'buttons.conf')
assert [home[f'key.{i}.action'] for i in range(6)]==['launch','launch','launch','update_all','cycle','next']
assert home['key.3.target']=='Scripts/update_all.sh'
assert home['key.1.target']=='/media/fat/_@Favorites/Donkey Kong Country (USA) (Rev 2).mgl'
assert home['key.2.target']=='_Console/MegaDrive_*.rbf'
assert not any(p.suffix in ['.jpg','.sda'] for p in icons.rglob('*'))
assert len(list((icons/'startup').glob('*.bmp')))==6

# Required secondary page rejection and malformed configuration index handling.
p=app/'buttons-confirm.conf';data=p.read_bytes()
try:
    p.unlink();assert run('buttons.conf','--validate').returncode!=0
    relative=subprocess.run([str(binary),'--config','buttons.conf','--assets',str(icons),'--validate'],cwd=app,capture_output=True)
    assert relative.returncode!=0
finally:p.write_bytes(data)
assert subprocess.run([str(binary),'--config','buttons.conf','--assets',str(icons),'--validate'],cwd=app,capture_output=True).returncode==0
p=app/'buttons-page2.conf';data=p.read_bytes()
try:
    p.write_bytes(data+b'key.6.action=launch\n');assert run('buttons.conf','--validate').returncode!=0
finally:p.write_bytes(data)

# Reject truncated/incorrect Mini BMP payloads and MK.2 codecs.
image=icons/home['key.0.image'];saved=image.read_bytes()
for corrupt in [saved[:-1],b'SDAN'+saved[4:],saved[:16]+b'ZZ'+saved[18:]]:
    try:
        image.write_bytes(corrupt);assert run('buttons.conf','--validate').returncode!=0
    finally:image.write_bytes(saved)

with tempfile.TemporaryDirectory() as temp:
    fat=Path(temp);(fat/'Scripts').mkdir();marker=fat/'should-not-exist'
    updater=fat/'Scripts/update_all.sh'
    updater.write_text('#!/bin/bash\ntouch '+str(marker)+'\n')
    result=run('buttons.conf','--root',str(fat),'--dry-run','3')
    assert result.returncode==0 and result.stdout.strip()=='bash '+str(updater)
    assert not marker.exists()
    updater.unlink();assert run('buttons.conf','--root',str(fat),'--dry-run','3').returncode!=0
    for key in ['6','14','-1']:assert run('buttons.conf','--root',str(fat),'--dry-run',key).returncode!=0
assert subprocess.run([str(binary),'--self-test'],capture_output=True).returncode==0
print('PASS: all six complete page configurations, every BMP frame/cache bound, malformed input rejection, first-page Update All dry-run, and preserved game targets')
