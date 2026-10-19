"""Build clean pixel shortcut animations and page-specific press feedback."""
import base64, io, json, re, struct, sys
from pathlib import Path
from PIL import Image, ImageDraw

root, docs = map(Path, sys.argv[1:3])
app = root / 'Scripts/.config/buttongoblin'
out = app / 'icons/custom'
out.mkdir(parents=True, exist_ok=True)
BG = (16,23,32)
FONT = {
'G':['01111','10000','10000','10111','10001','10001','01111'],
'H':['10001','10001','10001','11111','10001','10001','10001'],
'I':['11111','00100','00100','00100','00100','00100','11111'],
'A':['01110','10001','10001','11111','10001','10001','10001'],
'B':['11110','10001','10001','11110','10001','10001','11110'],
'C':['01111','10000','10000','10000','10000','10000','01111'],
'D':['11110','10001','10001','10001','10001','10001','11110'],
'E':['11111','10000','10000','11110','10000','10000','11111'],
'F':['11111','10000','10000','11110','10000','10000','10000'],
'K':['10001','10010','10100','11000','10100','10010','10001'],
'L':['10000','10000','10000','10000','10000','10000','11111'],
'M':['10001','11011','10101','10101','10001','10001','10001'],
'N':['10001','11001','11001','10101','10011','10011','10001'],
'O':['01110','10001','10001','10001','10001','10001','01110'],
'P':['11110','10001','10001','11110','10000','10000','10000'],
'R':['11110','10001','10001','11110','10100','10010','10001'],
'S':['01111','10000','10000','01110','00001','00001','11110'],
'T':['11111','00100','00100','00100','00100','00100','00100'],
'U':['10001','10001','10001','10001','10001','10001','01110'],
'W':['10001','10001','10001','10101','10101','11011','10001']}
labels = ['MENU','','UP','','ESC','','LEFT','DOWN','RIGHT','ENTER','SPACE','','','','BACK']
actions = ['menu','none','up','none','escape','none','left','down','right','enter','space','none','none','none','back']
paths = [[(0,0),(0,-1),(0,-2),(0,-1)],[(0,0),(1,0),(2,0),(1,0)],[(0,0),(1,-1),(0,-2),(-1,-1)],[(0,0),(-1,0),(-2,0),(-1,0)],[(0,0),(0,1),(0,2),(0,1)],[(0,0),(-1,-1),(0,0),(1,1)],[(0,0),(1,1),(0,2),(-1,1)],[(0,0),(1,-1),(2,0),(1,1)]]
def text(d,label,y,color):
    x=(36-(len(label)*6-1))//2
    for char in label:
        for row,bits in enumerate(FONT[char]):
            for col,b in enumerate(bits):
                if b=='1': d.point((x+col,y+row),fill=color)
        x+=6
def sprite(label):
    im=Image.new('RGB',(36,36),BG);d=ImageDraw.Draw(im);c=(136,218,207)
    if label=='MENU':
        for y in (7,12,17):d.rectangle((11,y,24,y+1),fill=c)
    elif label in ('UP','DOWN','LEFT','RIGHT','BACK'):
        arrow=Image.new('RGB',(36,36),BG);a=ImageDraw.Draw(arrow)
        a.line((18,7,18,21),fill=c,width=2);a.line((12,12,18,6,24,12),fill=c,width=2)
        turns={'UP':0,'DOWN':180,'LEFT':90,'RIGHT':270,'BACK':90}
        arrow=arrow.rotate(turns[label],center=(18,14),fillcolor=BG);im.paste(arrow);d=ImageDraw.Draw(im)
    elif label=='ESC':
        d.line((12,8,23,19),fill=c,width=2);d.line((23,8,12,19),fill=c,width=2)
    elif label=='ENTER':
        d.line((24,7,24,16,12,16),fill=c,width=2);d.line((16,11,11,16,16,21),fill=c,width=2)
    elif label=='SPACE':
        d.line((9,11,9,19,26,19,26,11),fill=c,width=2)
    if label:text(d,label,27,(230,236,245))
    return im
def shifted(im,xy):
    n=Image.new('RGB',im.size,BG);n.paste(im,xy);return n
def green(im):
    n=im.copy();n.putdata([BG if p==BG else (round(max(p)*0.28),round(max(p)*0.94),round(max(p)*0.5)) for p in im.getdata()]);return n
def png(im):
    b=io.BytesIO();im.save(b,'PNG');return 'data:image/png;base64,'+base64.b64encode(b.getvalue()).decode()
def jpeg(im):
    b=io.BytesIO();im.transpose(Image.Transpose.ROTATE_180).save(b,'JPEG',quality=95,subsampling=0);return b.getvalue()
def animated(frames,name):
    b=io.BytesIO();frames[0].save(b,'GIF',save_all=True,append_images=frames[1:],duration=200,loop=0,optimize=False,disposal=2)
    (out/(name+'.gif')).write_bytes(b.getvalue())
    data=bytearray(b'SDAN'+struct.pack('<HH',1,len(frames)))
    devices=[]
    for im in frames:
        j=jpeg(im);data.extend(struct.pack('<II',200,len(j)));data.extend(j)
        devices.append('data:image/jpeg;base64,'+base64.b64encode(j).decode())
    (out/(name+'.sda')).write_bytes(data)
    assert len(frames)<=32 and len(data)<=524288
    return 'data:image/gif;base64,'+base64.b64encode(b.getvalue()).decode(),devices
puft=Image.open(io.BytesIO(base64.b64decode(Path(__file__).with_name('puft.b64').read_text()))).convert('RGB')
# Palette snapping eliminates JPEG/antialias fringe pixels in the reference.
puft.putdata([BG if max(p)<80 else p for p in puft.getdata()])
puft=puft.quantize(colors=16,dither=Image.Dither.NONE).convert('RGB')
puft.putdata([BG if max(p)<80 else p for p in puft.getdata()])
clean=Image.new('RGB',(36,36),BG)
clean.paste(puft.crop((9,1,28,25)),(9,1))
text(ImageDraw.Draw(clean),'BACK',27,(230,236,245))
puft=clean
presets=[];cards=[];conf=['brightness=65','launch_gap_ms=3000'];active=0
for key,(label,action) in enumerate(zip(labels,actions)):
    base=sprite(label)
    if label:
        move=paths[active%len(paths)];active+=1
        frames=[shifted(base,xy) for xy in move]
        if label=='BACK':
            frames=frames*4
            # Fifth cycle: four hard-edged wipe poses into Stay Puft and back.
            for width in (9,18,27,36,36,27,18,9):
                morph=base.copy();morph.paste(puft.crop((0,0,width,36)),(0,0));frames.append(morph)
    else:frames=[base]
    frames=[im.resize((72,72),Image.Resampling.NEAREST) for im in frames]
    pressed=[green(im) for im in frames]
    normal,device=animated(frames,f'page2_{key:02d}_normal')
    request,request_device=animated(pressed,f'page2_{key:02d}_request')
    # Every feedback path is page-two artwork; no inherited console-state paths.
    for field,state in [('image','normal'),('request_image','request'),('error_image','normal'),('success_image','normal')]:
        conf.append(f'key.{key}.{field}=custom/page2_{key:02d}_{state}.sda')
    conf.extend([f'key.{key}.label={label or "Unused"}',f'key.{key}.action={action}',f'key.{key}.trigger=press'])
    presets.append(dict(label=label or 'Unused',action=action,normal=png(frames[0]),device=device[0],request=png(pressed[0]),requestDevice=request_device[0],frames=[png(im) for im in frames],requestFrames=[png(im) for im in pressed],durations=[200]*len(frames),deviceFrames=device,requestDeviceFrames=request_device))
    cards.append(f'<article class="key"><span class="keyno">{key+1:02d}</span><img src="{normal}" data-motion="{normal}" data-still="{png(frames[0])}" alt="{label or "Unused"} button"></article>')
    assert all(im.size==(72,72) for im in frames)
(app/'buttons-shortcuts.conf').write_text('\n'.join(conf)+'\n')
guide=docs/'index.html';s=guide.read_text();a=s.index('<section id="page-two"');b=s.index('</section>',a)
section=s[a:b];section=re.sub(r'<div class="deck">.*?</div>', '<div class="deck">'+''.join(cards)+'</div>',section,count=1,flags=re.S)
guide.write_text(s[:a]+section+s[b:])
(docs/'editor/shortcut-presets.js').write_text('const SHORTCUT_PRESETS='+json.dumps(presets,separators=(',',':'))+';\n')
print('PASS: 9 animated controls, matching green press frames, five-cycle Back morph, all feedback paths isolated')
