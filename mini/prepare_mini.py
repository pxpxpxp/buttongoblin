"""Prepare a Mini package from the tested v1 MK.2 artwork and scripts.

No GIF/BMP conversion runs on MiSTer. Native transform matches the upstream
python-elgato-streamdeck Mini driver: rotate 90 CCW, then flip top/bottom.
"""
import argparse, base64, io, json, shutil, struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from releases import MINI_VERSION, DUAL_VERSION, MINI_DOWNLOAD, DUAL_DOWNLOAD

PAGE_FILES=['buttons.conf','buttons-page2.conf','buttons-page3.conf','buttons-navigation.conf','buttons-shortcuts.conf','buttons-confirm.conf']
PAGE_TITLES=['Home','More consoles','More consoles','Navigation','Shortcuts','Y / N']
# Update All must remain on Home at physical key 4 (zero-based key.3).
LAYOUT=[
 [('console',0),('console',1),('console',2),('console',13),('console',14),('next',0)],
 [('console',3),('console',4),('console',5),('console',6),('console',7),('next',0)],
 [('console',8),('console',9),('console',10),('console',11),('console',12),('next',0)],
 [('shortcut',2),('shortcut',6),('shortcut',7),('shortcut',8),('shortcut',9),('next',0)],
 [('shortcut',0),('shortcut',4),('shortcut',10),('shortcut',14),('console',14),('next',0)],
 [('letter','Y'),('letter','N'),('blank',0),('shortcut',14),('blank',0),('next',0)],
]

def config(path):
    fields={}
    for line in path.read_text().splitlines():
        line=line.strip()
        if not line or line.startswith('#'):continue
        name,value=line.split('=',1);fields[name.strip()]=value.strip()
    return [{name.split('.',2)[2]:value for name,value in fields.items() if name.startswith(f'key.{key}.')} for key in range(15)]

def original_frames(path):
    data=path.read_bytes()
    if data[:4]==b'SDAN':
        version,count=struct.unpack_from('<HH',data,4);assert version==1 and 0<count<=32
        pos=8;frames=[]
        for _ in range(count):
            duration,size=struct.unpack_from('<II',data,pos);pos+=8
            image=Image.open(io.BytesIO(data[pos:pos+size])).convert('RGB').transpose(Image.Transpose.ROTATE_180)
            frames.append((image,max(duration,350)));pos+=size
        assert pos==len(data)
        return frames
    return [(Image.open(path).convert('RGB').transpose(Image.Transpose.ROTATE_180),350)]

def canvas(image):
    # Keep the original 72x72 pixel geometry exact inside the Mini's 80x80 key.
    out=Image.new('RGB',(80,80),(16,23,32))
    assert image.size==(72,72)
    out.paste(image,(4,4));return out

def bitmap(image):
    image=image.transpose(Image.Transpose.ROTATE_90).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    b=io.BytesIO();image.save(b,'BMP');data=b.getvalue()
    assert len(data)==19254 and data[:2]==b'BM'
    return data

def encode(frames,path):
    # Merge identical adjacent frames without changing animation timing.
    compact=[];previous=None
    for image,duration in frames:
        pixels=image.tobytes()
        if pixels==previous:
            compact[-1]=(compact[-1][0],compact[-1][1]+duration)
        else:
            compact.append((image,duration));previous=pixels
    frames=compact
    data=bytearray(b'SMAN'+struct.pack('<HH',1,len(frames)))
    for image,duration in frames:
        image_data=bitmap(image);data.extend(struct.pack('<II',duration,len(image_data)));data.extend(image_data)
    assert len(frames)<=32 and len(data)<=1048576
    path.write_bytes(data)

def preview(frames,path):
    images=[i for i,d in frames]
    images[0].save(path,format='GIF',save_all=True,append_images=images[1:],duration=[d for i,d in frames],loop=0,disposal=2,optimize=False)

def next_frames(green=False):
    frames=[]
    for dx in (0,1,2,1):
        im=Image.new('RGB',(80,80),(16,23,32));d=ImageDraw.Draw(im)
        color=(66,235,126) if green else (136,218,207)
        d.line((22+dx,26,55+dx,26),fill=color,width=4)
        d.line((44+dx,15,57+dx,26,44+dx,37),fill=color,width=4)
        font=ImageFont.load_default()
        d.text((40,56),'NEXT',font=font,anchor='mm',fill=color)
        frames.append((im,350))
    return frames

def letter_frames(letter,green=False):
    # Pixel lettering shares the shortcut palette and stays within safe margins.
    glyphs={'Y':['10001','10001','01010','00100','00100','00100','00100'],
            'N':['10001','11001','11001','10101','10011','10011','10001']}
    frames=[]
    for dy in (0,-1,-2,-1):
        im=Image.new('RGB',(80,80),(16,23,32));draw=ImageDraw.Draw(im)
        color=(66,235,126) if green else (224,233,244)
        for y,row in enumerate(glyphs.get(letter,[])):
            for x,pixel in enumerate(row):
                if pixel=='1':draw.rectangle((25+x*6,19+y*6+dy,30+x*6,24+y*6+dy),fill=color)
        frames.append((im,350))
    return frames

def prepare(base,output,docs=None):
    source=base/'Scripts/.config/streamdeck'
    app=output/'Scripts/.config/streamdeck-mini'
    if app.exists():shutil.rmtree(app)
    # Copy only reusable files, rather than copying and deleting MK.2 artwork,
    # source and configurations. The Mini owns a separate output tree.
    shutil.copytree(source,app,ignore=shutil.ignore_patterns('icons','source','*.conf','streamdeck-mister'))
    for operation in ['Enable','Disable']:
        name=f'StreamDeck_{operation}.sh'
        shutil.copy2(base/'Scripts'/name,output/'Scripts'/name)
    icons=app/'icons'
    icons.mkdir();(icons/'startup').mkdir();(app/'source').mkdir()
    shutil.copy2(Path(__file__).with_name('streamdeck-mini.c'),app/'source/streamdeck.c')
    test=Path(__file__).with_name('test_mini.c').read_text().replace('#include "streamdeck-mini.c"','#include "streamdeck.c"')
    (app/'source/test_mini.c').write_text(test)
    shutil.copy2(Path(__file__).with_name('test_package.py'),app/'source/test_package.py')
    for name in ['LICENSE.txt','ARTWORK_CREDITS.txt']:
        shutil.copy2(base/name,output/name)
    (icons/'blank.bmp').write_bytes(bitmap(Image.new('RGB',(80,80),(16,23,32))))
    console=config(source/'buttons.conf');shortcuts=config(source/'buttons-shortcuts.conf')
    labels=['NES','SNES','Genesis','TurboGrafx-16','PlayStation','Saturn','Nintendo 64','Mega CD','Game Boy Advance','Game Boy','Master System','Neo Geo','The Simpsons','Update All','Brightness']
    for key,entry in enumerate(console):
        entry.setdefault('label',labels[key])
        entry.setdefault('action','launch' if key<13 else 'update_all' if key==13 else 'cycle')
        entry.setdefault('trigger','press')
    converted={};summary=[]
    def convert(path):
        if path not in converted:
            frames=[(canvas(im),duration) for im,duration in original_frames(source/'icons'/path)]
            name=f'asset_{len(converted):03d}.sma'
            encode(frames,icons/name);converted[path]=(name,frames)
        return converted[path]
    def default_state(key,state,index):
        return key.get(state, f'{index:02d}_{"normal" if state=="image" else "request" if state=="request_image" else "error"}.jpg')
    for page,items in enumerate(LAYOUT):
        lines=['brightness=65','launch_gap_ms=3000'];page_summary=[]
        for key,(kind,index) in enumerate(items):
            if kind in ('letter','blank'):
                entry={'label':index if kind=='letter' else 'Blank','action':str(index).lower() if kind=='letter' else 'none','trigger':'press'}
                for state,green in [('image',False),('request_image',True),('error_image',False)]:
                    name=f'{kind}-{index}{"-green" if green else ""}.sma'
                    frames=letter_frames(index if kind=='letter' else '',green)
                    if not (icons/name).exists():encode(frames,icons/name)
                    entry[state]=name
                idle=letter_frames(index if kind=='letter' else '')
            elif kind=='next':
                entry={'label':'Next','action':'next','trigger':'release'}
                for state,green in [('image',False),('request_image',True),('error_image',False),('success_image',False)]:
                    name='next-green.sma' if green else 'next.sma';frames=next_frames(green)
                    if not (icons/name).exists():encode(frames,icons/name)
                    entry[state]=name
                idle=next_frames()
            else:
                original=(console if kind=='console' else shortcuts)[index];entry={k:v for k,v in original.items() if k in ['label','action','target','trigger','hold_ms']}
                for state in ['image','request_image','error_image','success_image']:
                    if state=='success_image' and entry['action']!='update_all':continue
                    path=original.get(state)
                    if not path:
                        path='custom/update_all_finished_gray.sda' if state=='success_image' else default_state(original,state,index)
                    name,frames=convert(path);entry[state]=name
                    if state=='image':idle=frames
                if entry['action'] in ['up','down','left','right','enter','menu','escape','space','back']:entry['trigger']='press'
            lines.extend(f'key.{key}.{field}={value}' for field,value in entry.items())
            gif_name=f'page{page+1}-key{key+1}.gif';preview(idle,icons/gif_name)
            page_summary.append({'label':entry['label'],'action':entry['action'],'image':gif_name})
        (app/PAGE_FILES[page]).write_text('\n'.join(lines)+'\n');summary.append(page_summary)
    assert summary[0][3]['action']=='update_all'
    # Recompose the original startup mosaic, preserving its aspect ratio.
    master=Image.new('RGB',(360,216))
    for key in range(15):
        im=original_frames(source/'icons/startup'/f'{key:02d}.jpg')[0][0]
        master.paste(im,((key%5)*72,(key//5)*72))
    boot=Image.new('RGB',(240,160));boot.paste(master.resize((240,144),Image.Resampling.NEAREST),(0,8))
    for key in range(6):
        x=(key%3)*80;y=(key//3)*80
        (icons/'startup'/f'{key:02d}.bmp').write_bytes(bitmap(boot.crop((x,y,x+80,y+80))))
    (app/'source/build.sh').write_text('#!/bin/sh\nset -eu\ncd "$(dirname "$0")/.."\nzig cc -target arm-linux-musleabihf -mcpu=cortex_a9 -static -O2 -s -Wall -Wextra -Werror source/streamdeck.c -o streamdeck-mister\n')
    diag=app/'extras/StreamDeck_Diagnostics.sh'
    diag.write_text(diag.read_text().replace('MK.2 v0.2.47',f'Mini v{MINI_VERSION}').replace('"$k" -le 14','"$k" -le 5'))
    text=diag.read_text()
    text=text.replace(" echo '\\nHID raw interfaces:'", """ echo '\\nUSB hub topology:'
 if command -v lsusb >/dev/null 2>&1; then lsusb -t 2>/dev/null || true; fi
 echo '\\nUSB speed, power and driver paths:'
 for u in /sys/bus/usb/devices/*; do
  [ -r "$u/idVendor" ] || continue
  printf '%s ' "$u"
  for field in idVendor idProduct speed bMaxPower; do
   [ ! -r "$u/$field" ] || { printf '%s=' "$field"; tr '\\n' ' ' < "$u/$field"; }
  done
  printf '\\n'
 done
 echo '\\nRecent USB/kernel messages:'
 dmesg 2>/dev/null | tail -n 150 || true
 echo '\\nHID raw interfaces:'""")
    diag.write_text(text)
    # Keep the Mini's installer, boot block and logs separate from the MK.2.
    for path in list((output/'Scripts').rglob('*.sh')):
        text=path.read_text().replace('/Scripts/.config/streamdeck','/Scripts/.config/streamdeck-mini')
        text=text.replace('/tmp/mister_streamdeck','/tmp/mister_streamdeck_mini')
        text=text.replace('STREAMDECK-MK2','STREAMDECK-MINI').replace('StreamDeck_','StreamDeck_Mini_').replace('.streamdeck-backup','.streamdeck-mini-backup')
        path.write_text(text)
        if path.name.startswith('StreamDeck_'):path.rename(path.with_name(path.name.replace('StreamDeck_','StreamDeck_Mini_',1)))
    for path in (output/'Scripts').rglob('*.sh'):path.chmod(0o755)
    info={'device':'Stream Deck Mini','version':MINI_VERSION,'page_files':PAGE_FILES,'pages':summary,'update_all':{'page':1,'physical_key':4,'target':'Scripts/update_all.sh'},'orientation':'rotate90CCW then flip top-bottom (upstream Mini driver)','frame_interval_ms':350}
    (output/'MINI_LAYOUT.json').write_text(json.dumps(info,indent=2)+'\n')
    (output/'README.txt').write_text(Path(__file__).with_name('README.md').read_text())
    if docs:
        mascot="data:image/webp;base64,"+base64.b64encode((Path(__file__).resolve().parent.parent/"docs/assets/button-goblin.webp").read_bytes()).decode()
        cards=[]
        for page,entries in enumerate(summary):
            page_cards=[]
            for k,e in enumerate(entries):
                data='data:image/gif;base64,'+base64.b64encode((icons/e['image']).read_bytes()).decode()
                page_cards.append(f'<div class="key"><img src="{data}" alt="{e["label"]}"></div>')
            cards.append(f'<section><h2>{page+1} · {PAGE_TITLES[page]}</h2><div class="deck">'+''.join(page_cards)+'</div></section>')
        page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Button Goblin · MiSTer Stream Deck Mini</title><style>html{color-scheme:dark}body{background:#0a1020;color:#eef4fc;font:16px/1.6 system-ui;max-width:760px;margin:40px auto;padding:0 20px}a{color:#74e5c7}h1{font-size:38px;line-height:1.1}.deck{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;padding:20px;background:#030609;border:1px solid #3c4350;border-radius:22px}.key{text-align:center;background:#101720;border-radius:10px;padding:12px}.key img{display:block;width:100%;max-width:160px;margin:auto;image-rendering:pixelated;aspect-ratio:1}section{margin:35px 0}.note{border-left:3px solid #74e5c7;padding:10px 18px;background:#172334}code{color:#a5d4fa}.goblin-mascot{display:block;width:260px;max-width:80%;height:auto;margin:0 auto 24px;image-rendering:pixelated;border-radius:24px}</style><img class="goblin-mascot" src="@GOBLIN_IMAGE@" width="1254" height="1254" alt="Button Goblin pressing a glowing six-key Stream Deck"><h1>Button Goblin</h1><p>MiSTer × Stream Deck Mini</p><p>Six keys. Animated consoles. Update All on the first page.</p><p><a href="@MINI_DOWNLOAD@">Download Mini v@MINI_VERSION@ ZIP</a> · <a href="https://github.com/pxpxpxp/MiSTer_StreamDeck/blob/main/mini/README.md">Installation and source</a> · <a href="./">15-key guide</a></p><div class="note">Physical key 4 on Home runs Update All. Key 6 cycles through all six pages; Shortcuts includes Back to Home. Tap Home’s brightness key for five ascending levels, or hold it one second to jump to Navigation. Keyboard shortcuts turn green while held. Page 6 adds Y and N for the active MiSTer screen; these keyboard events do not feed the background Update All process, which runs with non-interactive input.</div>'''+''.join(cards)+'''<h2>Use both decks</h2><p><a href="@DUAL_DOWNLOAD@">Download the Dual v@DUAL_VERSION@ bundle</a>, copy its complete Scripts contents, and run StreamDeck_Both_Enable.sh. The Mini and 15-key MK.2 have separate configurations, services and brightness.</p><h2>Install Mini only</h2><p>When upgrading Mini v1.0.0/v1.0.1, first run the old StreamDeck_Disable.sh. For later Mini versions, use StreamDeck_Mini_Disable.sh. Back up custom settings. Copy this Mini package’s Scripts contents, including hidden .config, into /media/fat/Scripts; run StreamDeck_Mini_Enable.sh. Install Update All separately at Scripts/update_all.sh.</p><p>This is a Mini-specific build. The 15-key editor and MK.2 artwork exports do not apply. The Mini build is host-tested and cross-compiled; physical Mini orientation, brightness, and USB playback need your hardware check.</p></html>'''
        for token,value in [('GOBLIN_IMAGE',mascot),('MINI_DOWNLOAD',MINI_DOWNLOAD),('DUAL_DOWNLOAD',DUAL_DOWNLOAD),('MINI_VERSION',MINI_VERSION),('DUAL_VERSION',DUAL_VERSION)]:
            page=page.replace(f'@{token}@',value)
        docs.parent.mkdir(parents=True,exist_ok=True);docs.write_text(page)
    print('Prepared Mini package: Update All on Home/key 4; six six-key pages; BMP animations')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('output',type=Path);p.add_argument('--guide',type=Path)
    a=p.parse_args();prepare(a.base,a.output,a.guide)
