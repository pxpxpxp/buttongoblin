"""Generate lossless upright editor defaults from a prepared Mini package."""
import base64, io, json, struct, sys
from pathlib import Path
from PIL import Image
FILES=['buttons.conf','buttons-page2.conf','buttons-page3.conf','buttons-navigation.conf','buttons-shortcuts.conf','buttons-confirm.conf']
def build(app, output, check=False):
    assets={}; pages=[]
    for filename in FILES:
        values=dict(line.split('=',1) for line in (app/filename).read_text().splitlines() if '=' in line and not line.startswith('#'))
        keys=[]
        for i in range(6):
            get=lambda field, default='':values.get(f'key.{i}.{field}',default)
            key=dict(label=get('label'), action=get('action'),target=get('target'),trigger=get('trigger','press'),hold=int(get('hold_ms','700')),art={})
            for state,field in [('normal','image'),('request','request_image'),('error','error_image'),('success','success_image')]:
                name=get(field)
                if not name:continue
                key['art'][state]=name
                if name in assets:continue
                data=(app/'icons'/name).read_bytes();frames=[];durations=[]
                if data[:4]==b'SMAN':
                    count=struct.unpack_from('<H',data,6)[0];pos=8
                    for _ in range(count):
                        delay,size=struct.unpack_from('<II',data,pos);pos+=8
                        frames.append(data[pos:pos+size]);durations.append(delay);pos+=size
                    assert pos==len(data)
                else:frames=[data];durations=[350]
                previews=[]
                for frame in frames:
                    image=Image.open(io.BytesIO(frame)).convert('RGB').transpose(Image.Transpose.TRANSPOSE)
                    buf=io.BytesIO();image.save(buf,'PNG')
                    previews.append('data:image/png;base64,'+base64.b64encode(buf.getvalue()).decode())
                assets[name]=dict(frames=previews,durations=durations)
            keys.append(key)
        pages.append(dict(brightness=int(values['brightness']),gap=int(values['launch_gap_ms']),keys=keys))
    result=dict(pages=pages,assets=assets)
    if check:
        old=json.loads(output.read_text().removeprefix('const MINI_DEFAULTS = ').strip().removesuffix(';'))
        assert old['pages']==pages and old['assets'].keys()==assets.keys()
        for name,asset in assets.items():
            previous=old['assets'][name]
            assert asset['durations']==previous['durations']
            assert len(asset['frames'])==len(previous['frames'])
            for a,b in zip(asset['frames'],previous['frames']):
                decode=lambda s:Image.open(io.BytesIO(base64.b64decode(s.split(',')[1]))).convert('RGB').tobytes()
                assert decode(a)==decode(b),name
        print('PASS: Mini editor defaults match all prepared page settings and artwork pixels')
    else:
        output.write_text('const MINI_DEFAULTS = '+json.dumps(result,separators=(',',':'))+';\n')
if __name__=='__main__':build(Path(sys.argv[1]),Path(sys.argv[2]),'--check' in sys.argv[3:])
