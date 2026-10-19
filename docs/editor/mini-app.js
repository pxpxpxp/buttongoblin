'use strict';
const ICON_SIZE=80;
const $=id=>document.getElementById(id);
function download(blob, name) {
    const url = URL.createObjectURL(blob),
        a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
}

function bytes(data) {
    return Uint8Array.from(atob(data.split(',')[1]), c => c.charCodeAt(0));
}
const crcTable = Uint32Array.from({
    length: 256
}, (_, n) => {
    for (let j = 0; j < 8; j++) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1;
    return n >>> 0;
});

function crc32(data) {
    let c = 0xffffffff;
    for (const x of data) c = crcTable[(c ^ x) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

// Store-only ZIP: bounded local assets need no compression library or service.
function zip(files) {
    const entries = [],
        central = [];
    let offset = 0,
        centralSize = 0;
    for (const [name, data] of files) {
        const n = new TextEncoder().encode(name),
            crc = crc32(data),
            h = new Uint8Array(30 + n.length),
            v = new DataView(h.buffer);
        v.setUint32(0, 0x04034b50, true);
        v.setUint16(4, 20, true);
        v.setUint16(6, 0x800, true);
        v.setUint16(12, 0x21, true);
        v.setUint32(14, crc, true);
        v.setUint32(18, data.length, true);
        v.setUint32(22, data.length, true);
        v.setUint16(26, n.length, true);
        h.set(n, 30);
        entries.push(h, data);
        const c = new Uint8Array(46 + n.length),
            w = new DataView(c.buffer);
        w.setUint32(0, 0x02014b50, true);
        w.setUint16(4, 20, true);
        w.setUint16(6, 20, true);
        w.setUint16(8, 0x800, true);
        w.setUint16(14, 0x21, true);
        w.setUint32(16, crc, true);
        w.setUint32(20, data.length, true);
        w.setUint32(24, data.length, true);
        w.setUint16(28, n.length, true);
        w.setUint32(42, offset, true);
        c.set(n, 46);
        central.push(c);
        centralSize += c.length;
        offset += h.length + data.length;
    }
    const end = new Uint8Array(22),
        v = new DataView(end.buffer);
    v.setUint32(0, 0x06054b50, true);
    v.setUint16(8, files.length, true);
    v.setUint16(10, files.length, true);
    v.setUint32(12, centralSize, true);
    v.setUint32(16, offset, true);
    return new Blob([...entries, ...central, end], {
        type: 'application/zip'
    });
}

const PAGE_FILES=['buttons.conf','buttons-page2.conf','buttons-page3.conf','buttons-navigation.conf','buttons-shortcuts.conf','buttons-confirm.conf'];
const STATES=['normal','request','error','success'];
const FIELDS=['image','request_image','error_image','success_image'];
const ACTIONS=['launch','script','update_all','cycle','dim','bright','reload','restart','space','menu','enter','up','down','left','right','escape','y','n','back','next','none'];
const KEYBOARD=['space','menu','enter','up','down','left','right','escape','y','n'];
const FORMAT='buttongoblin-mini-v1';
let activePage=0,selected=0,processing=false,project=defaults();
const deviceCache=new Map();
function defaults(){
 return {format:FORMAT,pages:MINI_DEFAULTS.pages.map(page=>({brightness:65,gap:3000,keys:page.keys.map(k=>{
 const key={label:k.label,action:k.action,target:k.target,trigger:k.trigger,hold:k.hold,icons:{},animations:{}};
 for(const [state,name] of Object.entries(k.art)){
 const a=MINI_DEFAULTS.assets[name];key.icons[state]=a.frames[0];key.animations[state]=structuredClone(a);
 }return key;})}))};
}
function page(){return project.pages[activePage];}
function key(){return page().keys[selected];}
function message(text,error=false){$('status').textContent=text;$('status').classList.toggle('error',error);}
function busy(value){processing=value;document.querySelectorAll('button,input,select').forEach(el=>el.disabled=value);if(!value)render();}
function icon(k,state){return k.icons[state]||k.icons.normal;}
function render(){
 $('deck').replaceChildren();page().keys.forEach((k,i)=>{
 const b=document.createElement('button');b.className='key'+(i===selected?' selected':'');b.setAttribute('aria-label',`Button ${i+1}: ${k.label}`);b.setAttribute('aria-pressed',String(i===selected));
 const im=document.createElement('img');im.src=icon(k,'normal');im.alt='';const n=document.createElement('span');n.className='number';n.textContent=i+1;b.append(im,n);
 b.onclick=()=>{selected=i;render();};$('deck').append(b);
 });
 const k=key();$('page').value=activePage;$('keyTitle').textContent=`Button ${selected+1} · key.${selected}`;$('position').textContent=`${selected<3?'Top':'Bottom'} row · ${selected%3+1}`;
 for(const field of ['label','action','target','trigger','hold'])$(field).value=k[field];
 const fixed=KEYBOARD.includes(k.action)||['next','back'].includes(k.action);$('trigger').disabled=processing||fixed;$('holdField').hidden=fixed||k.trigger!=='hold';
 $('targetField').hidden=!['launch','script','update_all'].includes(k.action);$('targetLabel').textContent=k.action==='launch'?'Core or game path':'Shell script path';
 $('targetHelp').textContent=k.action==='update_all'?'Installed Scripts/update_all.sh · background job · one-hour deadline. Log: /tmp/buttongoblin_mini.script.log.':'Paths start at /media/fat. Cores: .rbf, favorites: .mgl, arcade: .mra, scripts: .sh.';
 $('summaryLabel').textContent=k.label;$('summaryPath').textContent=k.target||k.action;$('summaryTrigger').textContent=`On ${k.trigger}${k.trigger==='hold'?` · ${k.hold} ms`:''}`;
 $('assigned').textContent=`${page().keys.filter(k=>k.action!=='none').length} active keys`;$('brightness').value=page().brightness;$('brightnessValue').textContent=page().brightness+'%';$('gap').value=page().gap;
 $('pageHelp').textContent='Six pages · button 6 advances on release. Home: Update All on button 4; brightness on button 5 (hold one second for Navigation). Back returns Home. Y/N control the active MiSTer screen.';
 $('iconPreview').src=icon(k,$('state').value);$('iconPreview').classList.remove('device');
}
function clean(s,max,empty=true){return typeof s==='string'&&(empty||s.length>0)&&new TextEncoder().encode(s).length<=max&&!/[\x00-\x1f\x7f]/.test(s)&&s===s.trim();}
function validImage(s){return typeof s==='string'&&s.length<=60000&&/^data:image\/(png|jpeg);base64,[A-Za-z0-9+/]+=*$/.test(s);}
function validate(p){
 if(!p||p.format!==FORMAT||!Array.isArray(p.pages)||p.pages.length!==6)throw Error('Open a Button Goblin Mini project with six pages. MK.2 projects use the 15-key editor.');
 for(const [n,page] of p.pages.entries()){
 if(!page||!Array.isArray(page.keys)||page.keys.length!==6)throw Error(`Page ${n+1} needs six buttons.`);
 if(!Number.isInteger(page.brightness)||page.brightness<0||page.brightness>100||!Number.isInteger(page.gap)||page.gap<100||page.gap>30000)throw Error('Brightness must be 0–100; cooldown must be 100–30000 ms.');
 let size=0;
 for(const [i,k] of page.keys.entries()){
 if(!k||!clean(k.label,95,false)||!clean(k.target,1000)||!ACTIONS.includes(k.action)||!['press','release','hold'].includes(k.trigger)||!Number.isInteger(k.hold)||k.hold<100||k.hold>10000)throw Error(`Check page ${n+1}, button ${i+1}: label, action, path or trigger.`);
 if(k.action==='launch'&&!/\.(rbf|mgl|mra)$/i.test(k.target))throw Error(`Page ${n+1}, button ${i+1} needs a .rbf, .mgl or .mra path.`);
 if(['script','update_all'].includes(k.action)&&!/\.sh$/i.test(k.target))throw Error('Script and Update All actions need a .sh path.');
 if(!k.icons||!validImage(k.icons.normal)||Object.entries(k.icons).some(([s,v])=>!STATES.includes(s)||!validImage(v)))throw Error('Invalid icon data.');
 if(!k.animations||typeof k.animations!=='object'||Array.isArray(k.animations))throw Error('Invalid animation data.');
 for(const [state,a] of Object.entries(k.animations)){
 if(!STATES.includes(state)||!a||!Array.isArray(a.frames)||!Array.isArray(a.durations)||a.frames.length<1||a.frames.length>32||a.frames.length!==a.durations.length||a.frames.some(f=>!validImage(f))||a.durations.some(d=>!Number.isInteger(d)||d<200||d>30000)||a.durations.reduce((s,d)=>s+d,0)>30000||k.icons[state]!==a.frames[0])throw Error('Invalid animation frames or timings.');
 }
 for(const state of STATES){const a=k.animations[state]||(!k.icons[state]?k.animations.normal:null);size+=a?8+a.frames.length*(19254+8):19254;}
 }if(size>4194304)throw Error(`Page ${n+1} exceeds the 4 MiB image cache. Shorten animations.`);
 }
 return p;
}
function validateNavigation(p){
 for(const [i,page] of p.pages.entries())if(page.keys[5].action!=='next')throw Error(`Keep page ${i+1} button 6 as Next so all pages stay reachable.`);
 if(p.pages[0].keys[3].action!=='update_all')throw Error('Keep Update All on Home button 4.');
 if(p.pages[0].keys[4].action!=='cycle')throw Error('Keep brightness cycle on Home button 5 for the Navigation hold shortcut.');
}
// Mini firmware expects a bottom-up, uncompressed 24-bit BMP. Its image
// transform is a transpose (rotate 90 CCW, then flip vertically).
function bitmapFromRGBA(rgba){
 const out=new Uint8Array(19254),v=new DataView(out.buffer);out.set([66,77]);v.setUint32(2,out.length,true);v.setUint32(10,54,true);v.setUint32(14,40,true);v.setInt32(18,80,true);v.setInt32(22,80,true);v.setUint16(26,1,true);v.setUint16(28,24,true);v.setUint32(34,19200,true);
 for(let y=0;y<80;y++)for(let x=0;x<80;x++){const src=(x*80+y)*4,dst=54+((79-y)*80+x)*3;out[dst]=rgba[src+2];out[dst+1]=rgba[src+1];out[dst+2]=rgba[src];}
 return out;
}
function renderIcon(image,addLabel,label,pixelArt=false){
 if(image.width<1||image.height<1||image.width*image.height>40000000)throw Error('Use an image under 40 megapixels.');
 const c=document.createElement('canvas');c.width=c.height=80;const ctx=c.getContext('2d');ctx.imageSmoothingEnabled=!pixelArt;ctx.fillStyle='#101720';ctx.fillRect(0,0,80,80);
 const size=Math.min(image.width,image.height);ctx.drawImage(image,(image.width-size)/2,(image.height-size)/2,size,size,0,0,80,80);
 if(addLabel){ctx.fillStyle='#000b';ctx.fillRect(0,58,80,22);ctx.fillStyle='#fff';ctx.font='bold 11px system-ui';ctx.textAlign='center';ctx.fillText(label,40,73,74);}
 return {normal:c.toDataURL('image/png'),device:bitmapFromRGBA(ctx.getImageData(0,0,80,80).data)};
}
async function decodeDevice(data){
 if(deviceCache.has(data))return deviceCache.get(data);
 const im=new Image();im.src=data;await im.decode();if(im.width!==80||im.height!==80)throw Error('Mini project frames must be 80 × 80.');
 const device=renderIcon(im,false,'',true).device;deviceCache.set(data,device);return device;
}
function miniAnimation(frames,durations){
 if(frames.length<1||frames.length>32||frames.length!==durations.length||frames.some(f=>f.length!==19254)||durations.some(d=>!Number.isInteger(d)||d<200||d>30000)||durations.reduce((s,d)=>s+d,0)>30000)throw Error('Invalid Mini animation.');
 const size=8+frames.length*(8+19254);if(size>1048576)throw Error('Animation exceeds 1 MiB.');const out=new Uint8Array(size),v=new DataView(out.buffer);out.set([83,77,65,78]);v.setUint16(4,1,true);v.setUint16(6,frames.length,true);let pos=8;
 frames.forEach((f,i)=>{v.setUint32(pos,durations[i],true);v.setUint32(pos+4,f.length,true);out.set(f,pos+8);pos+=8+f.length;});return out;
}
// gif.js calls this to validate converted frames before assigning them.
function animationBytes(frames,durations){return miniAnimation(frames,durations);}
async function exportFiles(){
 validate(project);validateNavigation(project);const files=[],root='Scripts/.config/buttongoblin-mini/',encoder=new TextEncoder();
 for(const [p,page] of project.pages.entries()){
 const lines=['# Button Goblin Mini — generated by Mini Editor',`brightness=${page.brightness}`,`launch_gap_ms=${page.gap}`];let pageSize=0;
 for(const [i,k] of page.keys.entries()){
 const trigger=KEYBOARD.includes(k.action)?'press':['next','back'].includes(k.action)?'release':k.trigger;
 lines.push('',`key.${i}.label=${k.label}`,`key.${i}.action=${k.action}`,`key.${i}.trigger=${trigger}`,`key.${i}.hold_ms=${k.hold}`);
 if(['launch','script','update_all'].includes(k.action))lines.push(`key.${i}.target=${k.target}`);
 for(const [s,state] of STATES.entries()){
 const a=k.animations[state]||k.animations.normal,frameData=k.icons[state]||k.icons.normal;
 let data,ext;if(k.animations[state]||!k.icons[state]&&a){const animation=k.animations[state]||a;data=miniAnimation(await Promise.all(animation.frames.map(decodeDevice)),animation.durations);ext='sma';}
 else{data=await decodeDevice(frameData);ext='bmp';}
 const name=`editor/p${p+1}_k${i+1}_${state}.${ext}`;files.push([root+'icons/'+name,data]);lines.push(`key.${i}.${FIELDS[s]}=${name}`);pageSize+=data.length;
 }
 }if(pageSize>4194304)throw Error(`Page ${p+1} exceeds the 4 MiB cache.`);
 files.push([root+PAGE_FILES[p],encoder.encode(lines.join('\n')+'\n')]);
 }
 files.push(['READ_ME.txt',encoder.encode('Button Goblin Mini v1.1.0 layout: all six page configurations and prepared 80x80 BMP/SMAN artwork. Install the Mini launcher separately. Wait for Update All to finish. Run Scripts/ButtonGoblin_Mini_Disable.sh, merge exported Scripts into /media/fat/Scripts (include hidden .config), then run Scripts/ButtonGoblin_Mini_Enable.sh. Keep the installed binary. Home button 4: Update All. Home button 5: brightness; hold one second for Navigation. Button 6: Next. Back: Home. Y/N go to the active screen, not background updater input. Save project JSON to preserve editable artwork.\n')]);return files;
}
function frameAt(k,state,now){const a=k.animations[state]||(!k.icons[state]?k.animations.normal:null);if(!a)return icon(k,state);let t=now%a.durations.reduce((s,d)=>s+d,0);for(let i=0;i<a.frames.length;i++){if(t<a.durations[i])return a.frames[i];t-=a.durations[i];}return a.frames[0];}
$('page').onchange=()=>{activePage=Number($('page').value);selected=0;render();};
for(const field of ['label','target','hold'])$(field).oninput=()=>{key()[field]=field==='hold'?Number($(field).value):$(field).value;$('summaryLabel').textContent=key().label;$('summaryPath').textContent=key().target||key().action;};
for(const field of ['action','trigger'])$(field).onchange=()=>{key()[field]=$(field).value;if(KEYBOARD.includes(key().action))key().trigger='press';if(['next','back'].includes(key().action))key().trigger='release';render();};
$('state').onchange=render;
$('brightness').oninput=()=>{const value=Number($('brightness').value);project.pages.forEach(p=>p.brightness=value);$('brightnessValue').textContent=value+'%';};
$('gap').oninput=()=>project.pages.forEach(p=>p.gap=Number($('gap').value));
$('iconFile').onchange=async()=>{
 const f=$('iconFile').files[0];if(!f)return;busy(true);
 try{
 if(f.size>12000000||!['image/png','image/jpeg','image/webp','image/gif'].includes(f.type))throw Error('Choose PNG, JPG, WebP or GIF under 12 MB.');
 const k=key(),state=$('state').value;let result;
 if(f.type==='image/gif'){message('Preparing Mini animation…');result=await normaliseGif(await f.arrayBuffer(),$('addLabel').checked,k.label);miniAnimation(result.device,result.durations);k.animations[state]={frames:result.frames,durations:result.durations};k.icons[state]=result.frames[0];result.frames.forEach((frame,i)=>deviceCache.set(frame,result.device[i]));}
 else{const data=await new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(r.result);r.onerror=()=>reject(Error('Image could not be read.'));r.readAsDataURL(f);});const im=new Image();im.src=data;await im.decode();result=renderIcon(im,$('addLabel').checked,k.label);delete k.animations[state];k.icons[state]=result.normal;deviceCache.set(result.normal,result.device);}
 message('Mini artwork assigned.');
 }catch(e){message(e.message,true);}finally{$('iconFile').value='';busy(false);}
};
$('restoreIcon').onclick=()=>{const original=defaults().pages[activePage].keys[selected],state=$('state').value;delete key().icons[state];delete key().animations[state];if(original.icons[state]){key().icons[state]=original.icons[state];key().animations[state]=original.animations[state];}render();message('Default artwork restored.');};
function applyPreset(index){const originals=defaults().pages.flatMap(p=>p.keys);const names=['NES','SNES','Genesis','TG16','PlayStation','Saturn','N64','MegaCD','GBA','GameBoy','MasterSystem','NeoGeo','Simpsons','Brightness'];const source=originals.find(k=>k.label===names[index]);if(!source)return;key().icons.normal=source.icons.normal;key().animations.normal=source.animations.normal;render();message('Animation preset assigned.');}
$('consolePreset').onclick=()=>applyPreset(Number($('consoleChoice').value));$('marioPreset').onclick=()=>applyPreset(0);
$('export').onclick=async()=>{busy(true);try{message('Preparing all six pages…');download(zip(await exportFiles()),'ButtonGoblin_Mini_Layout.zip');message('Mini layout downloaded. Disable Mini, copy exported Scripts, then enable Mini.');}catch(e){message(e.message,true);}finally{deviceCache.clear();busy(false);}};
$('save').onclick=()=>{try{validate(project);download(new Blob([JSON.stringify(project)],{type:'application/json'}),'ButtonGoblin_Mini_Project.json');message('All six Mini pages saved.');}catch(e){message(e.message,true);}};
$('import').onclick=()=>$('projectFile').click();
$('projectFile').onchange=async()=>{const f=$('projectFile').files[0];if(!f)return;busy(true);try{if(f.size>16000000)throw Error('Project must be under 16 MB.');const candidate=validate(JSON.parse(await f.text()));const unique=new Set();for(const p of candidate.pages)for(const k of p.keys){Object.values(k.icons).forEach(f=>unique.add(f));Object.values(k.animations).forEach(a=>a.frames.forEach(f=>unique.add(f)));}for(const frame of unique)await decodeDevice(frame);project=candidate;activePage=selected=0;message('All six Mini pages opened.');}catch(e){message('Could not open project: '+e.message,true);}finally{deviceCache.clear();$('projectFile').value='';busy(false);}};
$('reset').onclick=()=>{if(!confirm('Reset all six Mini pages? Save your project first to keep your edits.'))return;project=defaults();activePage=selected=0;deviceCache.clear();render();message('Default Mini layout restored.');};
window.setInterval?.(()=>{if(document.hidden||processing)return;const now=Date.now();$('deck').querySelectorAll('img').forEach((im,i)=>{const src=frameAt(page().keys[i],'normal',now);if(im.src!==src)im.src=src;});$('iconPreview').src=frameAt(key(),$('state').value,now);},200);
render();
