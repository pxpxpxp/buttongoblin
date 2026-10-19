'use strict';
const $ = id => document.getElementById(id);
const STATES = ['normal', 'request', 'error'];
const ACTIONS = ['launch', 'script', 'dim', 'bright', 'reload', 'restart', 'none', 'cycle', 'update_all', 'space', 'menu', 'enter', 'up', 'down', 'left', 'right', 'escape', 'back'];
const SHORTCUT_ACTIONS=['space','menu','enter','up','down','left','right','escape','back','none'];
const LABELS = ['NES', 'SNES', 'Genesis', 'TG16', 'PlayStation', 'Saturn', 'N64', 'MegaCD', 'GBA', 'GameBoy', 'MasterSystem', 'NeoGeo', 'Simpsons', 'Update All', 'Brightness'];
const TARGETS = ['_Console/NES_*.rbf', "/media/fat/_@Favorites/Donkey Kong Country (USA) (Rev 2).mgl", '_Console/MegaDrive_*.rbf', '_Console/TurboGrafx16_*.rbf', '_Console/PSX_*.rbf', '_Console/Saturn_*.rbf', '_Console/N64_*.rbf', '_Console/MegaCD_*.rbf', '_Console/GBA_*.rbf', '_Console/Gameboy_*.rbf', '_Console/SMS_*.rbf', '_Console/NeoGeo_*.rbf', '_Arcade/The Simpsons (4 Players World, set 1).mra', 'Scripts/update_all.sh'];
let project = defaults(),
    pageProjects = [project, shortcutDefaults()],
    activePage = 0,
    selected = 0,
    uploadSerial = 0,
    processing = false;

function setProcessing(value) {
    processing = value;
    document.querySelectorAll('button,input,select').forEach(el => el.disabled = value);
    if(!value)render();
}

// Project data contains upright preview images; rotated device images are
// derived caches. Saved JSON omits device caches and hydrate rebuilds them.
function defaults() {
    const p = {
        format: 'buttongoblin-v1',
        brightness: 65,
        gap: 3000,
        keys: LABELS.map((label, i) => ({
            label,
            action: i < 13 ? 'launch' : i === 13 ? 'update_all' : 'cycle',
            target: TARGETS[i] || '',
            trigger: 'press',
            hold: 700,
            icons: {}
        }))
    };
    p.keys.slice(0, 13).forEach((key, i) => applyConsolePreset(key, i));
    applyConsolePreset(p.keys[14], 13);
    const updater = p.keys[13];
    updater.icons.request = UPDATE_ALL_PRESET.frames[0];
    updater.deviceIcons = {request: UPDATE_ALL_PRESET.device[0]};
    updater.animations = {request: {frames: [...UPDATE_ALL_PRESET.frames], durations: [...UPDATE_ALL_PRESET.durations]}};
    updater.deviceAnimations = {request: [...UPDATE_ALL_PRESET.device]};
    return p;
}

function shortcutDefaults(){return {format:'buttongoblin-v1',brightness:65,gap:3000,keys:SHORTCUT_PRESETS.map(p=>({label:p.label,action:p.action,target:'',trigger:'press',hold:700,icons:{normal:p.normal,request:p.request,error:p.normal},deviceIcons:{normal:p.device,request:p.requestDevice,error:p.device},animations:{normal:{frames:[...p.frames],durations:[...p.durations]},request:{frames:[...p.requestFrames],durations:[...p.durations]},error:{frames:[...p.frames],durations:[...p.durations]}},deviceAnimations:{normal:[...p.deviceFrames],request:[...p.requestDeviceFrames],error:[...p.deviceFrames]}}))};}
function selectPage(page){if(processing)return;pageProjects[activePage]=project;pageProjects.forEach(p=>{p.brightness=project.brightness;p.gap=project.gap;});activePage=page;project=pageProjects[page];selected=0;uploadSerial++;render();}
function assetName(i){return (activePage===1?'page2_':'')+i;}
function validateShortcuts(p){validate(p);if(p.keys.some(k=>!SHORTCUT_ACTIONS.includes(k.action)))throw Error('Page two supports keyboard shortcuts, Back, or no action. Put launchers and scripts on page one.');return p;}
function completeProject(){pageProjects[activePage]=project;pageProjects.forEach(p=>{p.brightness=project.brightness;p.gap=project.gap;});const saved=structuredClone(pageProjects[0]);saved.shortcuts=structuredClone(pageProjects[1]);validateShortcuts(saved.shortcuts);for(const p of [saved,saved.shortcuts]){validate(p);p.keys.forEach(k=>{delete k.deviceIcons;delete k.deviceAnimations;});}return saved;}
function message(s, error = false) {
    $('status').textContent = s;
    $('status').classList.toggle('error', error);
}

function iconData(i, state) {
    return project.keys[i].icons[state] || BUILTINS[`${String(i).padStart(2,'0')}_${state}.jpg`];
}

function render() {
    $('deck').replaceChildren();
    project.keys.forEach((k, i) => {
        const b = document.createElement('button');
        b.className = 'key' + (i === selected ? ' selected' : '') + (k.icons.normal ? '' : ' device');
        b.setAttribute('aria-label', `Button ${i+1}: ${k.label}`);
        b.setAttribute('aria-pressed', String(i === selected));
        const im = document.createElement('img');
        im.src = iconData(i, 'normal');
        im.alt = '';
        const n = document.createElement('span');
        n.className = 'number';
        n.textContent = i+1;
        b.append(im, n);
        b.onclick = () => {
            selected = i;
            uploadSerial++;
            render();
        };
        $('deck').append(b);
    });
    const k = project.keys[selected];
    $('keyTitle').textContent = `Button ${selected+1} · key.${selected}`;
    $('page').value=String(activePage);
    $('action').querySelectorAll('option').forEach(option=>option.disabled=activePage===1&&!SHORTCUT_ACTIONS.includes(option.value));
    $('pageHelp').textContent=activePage===0?'Page 1: tap button 15 for brightness; hold for one second to open shortcuts.':'Page 2: keyboard shortcuts follow press/release. Keep a Back button to return.';
    $('position').textContent = `${['Top','Middle','Bottom'][Math.floor(selected/5)]} row · ${selected%5+1}`;
    for (const name of ['label', 'action', 'target', 'trigger', 'hold']) $(name).value = k[name];
    $('targetField').hidden = !['launch', 'script', 'update_all'].includes(k.action);
    const shortcut=['space','menu','enter','up','down','left','right','escape','back'].includes(k.action);
    $('trigger').disabled=shortcut;
    $('holdField').hidden = shortcut || k.trigger !== 'hold';
    $('targetLabel').textContent = ['script', 'update_all'].includes(k.action) ? 'Shell script path' : 'Core or game path';
    $('targetHelp').textContent = k.action === 'update_all' ? 'Installed Scripts/update_all.sh · background job · one-hour deadline. Progress: /tmp/buttongoblin.script.log.' : ['script', 'update_all'].includes(k.action) ? 'Example: Scripts/MyScript.sh · .sh files only.' : 'Example: _Console/SNES_*.rbf or _GameLaunchers/My Game.mgl · .rbf, .mgl, .mra';
    $('summaryLabel').textContent = k.label || `Key ${selected}`;
    $('summaryPath').textContent = ['launch', 'script', 'update_all'].includes(k.action) ? k.target || 'Set a path to continue' : k.action;
    $('summaryTrigger').textContent = `${k.trigger==='hold'?`Hold for ${k.hold} ms`:`On ${k.trigger}`} · ${['script','update_all'].includes(k.action)?'Shell script':k.action==='launch'?'Core / game launcher':'Deck control'}`;
    $('assigned').textContent = `${project.keys.filter(k=>k.action!=='none').length} active keys`;
    $('brightness').value = project.brightness;
    $('brightnessValue').textContent = project.brightness + '%';
    $('gap').value = project.gap;
    const state = $('state').value;
    $('iconPreview').src = iconData(selected, state);
    $('iconPreview').classList.toggle('device', !k.icons[state]);
}

function clean(s, max, allowEmpty = true) {
    return typeof s === 'string' && (allowEmpty || s.length > 0) && new TextEncoder().encode(s).length <= max && !/[\x00-\x1f\x7f]/.test(s) && s === s.trim();
}

// Reject invalid imported/agent-supplied values before mutating the live layout.
function validate(p) {
    if (!p || p.format !== 'buttongoblin-v1' || !Array.isArray(p.keys) || p.keys.length !== 15) throw Error('Open a Button Goblin project with exactly 15 keys.');
    if (!Number.isInteger(p.brightness) || p.brightness < 0 || p.brightness > 100 || !Number.isInteger(p.gap) || p.gap < 100 || p.gap > 30000) throw Error('Brightness must be 0–100 and cooldown 100–30000 ms.');
    p.keys.forEach((k, i) => {
        if (!k || !clean(k.label, 95, false) || !clean(k.target, 1000) || !ACTIONS.includes(k.action) || !['press', 'release', 'hold'].includes(k.trigger) || !Number.isInteger(k.hold) || k.hold < 100 || k.hold > 10000) throw Error(`Check the label, path and trigger on key ${i}.`);
        if (k.action === 'launch' && !/\.(rbf|mgl|mra)$/i.test(k.target)) throw Error(`Key ${i} needs a .rbf, .mgl or .mra path.`);
        if (['script', 'update_all'].includes(k.action) && !/\.sh$/i.test(k.target)) throw Error(`Key ${i} needs a .sh script path.`);
        if (!k.icons || Object.keys(k.icons).some(s => !STATES.includes(s)) || Object.values(k.icons).some(x => typeof x !== 'string' || !/^data:image\/(?:jpeg|png);base64,[A-Za-z0-9+/]+=*$/.test(x) || x.length > 45000)) throw Error(`Invalid icon on key ${i}.`);
    });
    validateAnimations(p);
    return p;
}

function configText() {
    validate(project);
    const lines = ['# Button Goblin MK.2 — generated by Deck Editor', 'brightness=' + project.brightness, 'launch_gap_ms=' + project.gap];
    project.keys.forEach((k, i) => {
        lines.push('', `key.${i}.label=${k.label}`, `key.${i}.action=${k.action}`, `key.${i}.trigger=${k.trigger}`, `key.${i}.hold_ms=${k.hold}`);
        if (['launch', 'script', 'update_all'].includes(k.action)) lines.push(`key.${i}.target=${k.target}`);
        if (k.action === 'update_all') lines.push(`key.${i}.success_image=custom/update_all_finished_gray.sda`);
        STATES.forEach((state, s) => lines.push(`key.${i}.${['image','request_image','error_image'][s]}=${k.animations?.[state]?`custom/${assetName(i)}_${state}.sda`:k.icons[state]?`custom/${assetName(i)}_${state}.jpg`:`${String(i).padStart(2,'0')}_${state}.jpg`}`));
    });
    return lines.join('\n') + '\n';
}

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



function exportPageFiles() {
    const encoder = new TextEncoder(),
        root = 'Scripts/.config/buttongoblin/',
        files = [
            [root + (activePage===1?'buttons-shortcuts.conf':'buttons.conf'), encoder.encode(configText())]
        ];
    // Match the launcher's unique-path cache, excluding unused built-in artwork.
    const cached = new Set();
    project.keys.forEach((k, i) => {
        STATES.forEach(state => cached.add(k.animations?.[state] ? `custom/${assetName(i)}_${state}.sda` : k.icons[state] ? `custom/${assetName(i)}_${state}.jpg` : `${String(i).padStart(2,'0')}_${state}.jpg`));
        if (k.action === 'update_all') cached.add('custom/update_all_finished_gray.sda');
    });
    for (const name of cached) if (Object.hasOwn(BUILTINS, name)) files.push([root + 'icons/' + name, bytes(BUILTINS[name])]);
    project.keys.forEach((k, i) => STATES.forEach(s => {
        if (k.animations?.[s]) {
            files.push([root + `icons/custom/${assetName(i)}_${s}.sda`, animationBytes(k.deviceAnimations[s], k.animations[s].durations)]);
        } else if (k.icons[s]) {
            files.push([root + `icons/custom/${assetName(i)}_${s}.jpg`, bytes(k.deviceIcons[s])]);
        }
    }));
    const imageFiles = new Map(files.filter(([name]) => name.startsWith(root + 'icons/')).map(([name, data]) => [name.slice((root + 'icons/').length), data]));
    let cacheBytes = 0;
    for (const name of cached) {
        if (!imageFiles.has(name)) throw Error(`Missing image: ${name}`);
        cacheBytes += imageFiles.get(name).length;
    }
    if (cacheBytes > 4194304) throw Error('Deck image cache exceeds 4 MiB. Use fewer/smaller animations.');
    return files;
}
function exportFiles(){
 pageProjects[activePage]=project;pageProjects.forEach(p=>{p.brightness=project.brightness;p.gap=project.gap;});
 if(pageProjects[0].keys[14].action!=='cycle')throw Error('Keep page 1 button 15 as brightness cycle to open page two.');
 validateShortcuts(pageProjects[1]);
 if(!pageProjects[1].keys.some(k=>k.action==='back'))throw Error('Page two needs a Back button.');
 const view=project,page=activePage;const files=[];
 try{for(let i=0;i<2;i++){activePage=i;project=pageProjects[i];files.push(...exportPageFiles());}}finally{project=view;activePage=page;}
 const unique=new Map();for(const [name,data] of files){if(!unique.has(name))unique.set(name,data);}
 unique.set('READ_ME.txt',new TextEncoder().encode('Requires Button Goblin v1.1.1. Both page configurations and artwork are included. Extract into /media/fat, including hidden Scripts/.config, and run ButtonGoblin_Enable.sh after any updater finishes. Tap button 15 for brightness; hold one second for page two. Back returns to consoles. Keep your launcher binary; this export contains only configuration/artwork. Save project JSON to preserve both editable pages.\n'));
 return [...unique];
}

async function normaliseIcon(data, addLabel, label, pixelArt = false) {
    const image = new Image();
    image.src = data;
    await image.decode();
    return renderIcon(image, addLabel, label, pixelArt);
}
// Reuse decoded images/canvases directly; no intermediate PNG round-trip.
function renderIcon(image, addLabel, label, pixelArt = false) {
    if (image.width < 1 || image.height < 1 || image.width * image.height > 40000000) throw Error('Image is too large. Use an image under 40 megapixels.');
    const c = document.createElement('canvas');
    c.width = c.height = 72;
    const ctx = c.getContext('2d');
    // GIF sprites use nearest-neighbour sampling; still images retain smoothing.
    ctx.imageSmoothingEnabled = !pixelArt;
    ctx.fillStyle = '#101418';
    ctx.fillRect(0, 0, 72, 72);
    const size = Math.min(image.width, image.height);
    ctx.drawImage(image, (image.width - size) / 2, (image.height - size) / 2, size, size, 0, 0, 72, 72);
    if (addLabel) {
        ctx.fillStyle = '#000b';
        ctx.fillRect(0, 51, 72, 21);
        ctx.fillStyle = '#fff';
        ctx.font = 'bold 10px system-ui';
        ctx.textAlign = 'center';
        ctx.fillText(label, 36, 65, 66);
    }
    const normal = c.toDataURL('image/jpeg', .9);
    const d = document.createElement('canvas');
    d.width = d.height = 72;
    const dc = d.getContext('2d');
    dc.translate(72, 72);
    dc.rotate(Math.PI);
    dc.drawImage(c, 0, 0);
    const device = d.toDataURL('image/jpeg', .9);
    if (bytes(device).length > 32768) throw Error('Icon exceeds the device limit.');
    return {
        normal,
        device
    };
}
// Build a candidate project completely so a failed decode preserves current edits.
async function hydratePage(p) {
    validate(p);
    const next = structuredClone(p);
    for (const k of next.keys) {
        // Move paths inside the older bundled app directory to its stable root.
        // Preserve unrelated game/script targets and the relative/absolute form.
        if (k.action === 'launch' && k.target.replace(/^\/media\/fat\//, '') === '_GameLaunchers/Donkey Kong Country.mgl') k.target = "/media/fat/_@Favorites/Donkey Kong Country (USA) (Rev 2).mgl";
        if (k.action === 'launch' && k.target === '_Console/Genesis_*.rbf') k.target = '_Console/MegaDrive_*.rbf';
        if (k.action === 'update_all' && k.target.replace(/^\/media\/fat\//, '') === 'Scripts/.config/buttongoblin/extras/ButtonGoblin_Update_All.sh') k.target = 'Scripts/update_all.sh';
        k.deviceIcons = {};
        k.deviceAnimations = {};
        for (const [state, animation] of Object.entries(k.animations || {})) {
            const frames = [];
            for (const frame of animation.frames) {
                const im = new Image();
                im.src = frame;
                await im.decode();
                if (im.width !== 72 || im.height !== 72) throw Error('Animation frames must be 72 × 72.');
                frames.push(renderIcon(im, false, '', true).device);
            }
            animationBytes(frames, animation.durations);
            k.deviceAnimations[state] = frames;
            k.icons[state] = animation.frames[0];
        }
        for (const state of STATES)
            if (k.icons[state] && !k.animations?.[state]) {
                const im = new Image();
                im.src = k.icons[state];
                await im.decode();
                if (im.width !== 72 || im.height !== 72) throw Error('Project icons must be 72 × 72.');
                const result = renderIcon(im, false, '');
                k.deviceIcons[state] = result.device;
            }
    }
    return next;
}
async function hydrate(p){const next=await hydratePage(p);if(p.shortcuts){validateShortcuts(p.shortcuts);next.shortcuts=await hydratePage(p.shortcuts);}else next.shortcuts=shortcutDefaults();return next;}
$('page').onchange=()=>selectPage(Number($('page').value));
for (const name of ['label', 'target', 'hold']) $(name).addEventListener('input', () => {
    project.keys[selected][name] = name === 'hold' ? Number($(name).value) : $(name).value; /* Preserve focus and typing position. */
    $('summaryLabel').textContent = project.keys[selected].label;
    $('summaryPath').textContent = ['launch', 'script', 'update_all'].includes(project.keys[selected].action) ? project.keys[selected].target || 'Set a path to continue' : project.keys[selected].action;
});
for (const name of ['action', 'trigger']) $(name).onchange = () => {
    project.keys[selected][name] = $(name).value;
    if(['space','menu','enter','up','down','left','right','escape','back'].includes(project.keys[selected].action))project.keys[selected].trigger='press';
    render();
};
$('state').onchange = render;
$('brightness').oninput = () => {
    project.brightness = Number($('brightness').value);
    $('brightnessValue').textContent = project.brightness + '%';
};
$('gap').oninput = () => project.gap = Number($('gap').value);
$('iconFile').onchange = async () => {
    const f = $('iconFile').files[0],
        i = selected,
        state = $('state').value,
        serial = ++uploadSerial;
    if (!f) return;
    setProcessing(true);
    try {
        if (f.size > 12000000 || !['image/png', 'image/jpeg', 'image/webp', 'image/gif'].includes(f.type)) throw Error('Choose a PNG, JPG, WebP or GIF under 12 MB.');
        if (f.type === 'image/gif') {
            message('Preparing animation…');
            const result = await normaliseGif(await f.arrayBuffer(), $('addLabel').checked, project.keys[i].label);
            animationBytes(result.device, result.durations);
            if (serial !== uploadSerial) return;
            project.keys[i].animations ??= {};
            project.keys[i].deviceAnimations ??= {};
            project.keys[i].animations[state] = {
                frames: result.frames,
                durations: result.durations
            };
            project.keys[i].deviceAnimations[state] = result.device;
            project.keys[i].icons[state] = result.frames[0];
            render();
            message(`Animation assigned: ${result.frames.length} frames, up to 5 fps.`);
            return;
        }
        const data = await new Promise((resolve, reject) => {
            const r = new FileReader();
            r.onload = () => resolve(r.result);
            r.onerror = () => reject(Error('Could not read image.'));
            r.readAsDataURL(f);
        });
        const result = await normaliseIcon(data, $('addLabel').checked, project.keys[i].label);
        if (serial !== uploadSerial) return;
        clearIconState(project.keys[i], state);
        project.keys[i].icons[state] = result.normal;
        project.keys[i].deviceIcons ??= {};
        project.keys[i].deviceIcons[state] = result.device;
        render();
        message(`Icon assigned to key ${i}.`);
    } catch (e) {
        message(e.message, true);
    } finally {
        $('iconFile').value = '';
        setProcessing(false);
    }
};
// A state may have either a static icon or animation. Clear both derived caches
// when replacing it so old artwork cannot survive in an inactive representation.
function clearIconState(key, state) {
    for (const field of ['icons', 'deviceIcons', 'animations', 'deviceAnimations']) {
        if (key[field]) delete key[field][state];
    }
}
$('restoreIcon').onclick = () => {
    uploadSerial++;
    const key = project.keys[selected],
        state = $('state').value;
    clearIconState(key, state);
    render();
    message('Default icon restored.');
};
$('export').onclick = () => {
    try {
        download(zip(exportFiles()), 'ButtonGoblin_Layout.zip');
        message('Export downloaded. Copy all exported folders onto /media/fat, then enable.');
    } catch (e) {
        message(e.message, true);
    }
};
$('save').onclick = () => {
    try {
        validate(project);
        const p = completeProject();
        download(new Blob([JSON.stringify(p, null, 2)], {
            type: 'application/json'
        }), 'ButtonGoblin_Project.json');
        message('Project saved, including your custom icons.');
    } catch (e) {
        message(e.message, true);
    }
};
$('import').onclick = () => $('projectFile').click();
$('projectFile').onchange = async () => {
    const f = $('projectFile').files[0];
    if (!f) return;
    const serial = ++uploadSerial;
    setProcessing(true);
    try {
        if (f.size > 16000000) throw Error('Project file is too large.');
        const p = await hydrate(JSON.parse(await f.text()));
        if (serial !== uploadSerial) return;
        pageProjects=[p,p.shortcuts];delete p.shortcuts;activePage=0;project=pageProjects[0];
        selected = 0;
        render();
        message('Project opened.');
    } catch (e) {
        message('Could not open project: ' + e.message, true);
    } finally {
        $('projectFile').value = '';
        setProcessing(false);
    }
};
$('reset').onclick = () => {
    if (!confirm('Reset both pages? Save your project first if you want to keep it.')) return;
    uploadSerial++;
    project = defaults();pageProjects=[project,shortcutDefaults()];activePage=0;
    selected = 0;
    render();
    message('Default layout restored.');
};
const modelContext = document.modelContext;
const toolLife = new AbortController();

function register(tool) {
    try {
        Promise.resolve(modelContext.registerTool(tool, {
            signal: toolLife.signal
        })).catch(() => {});
    } catch {}
}
if (modelContext?.registerTool) {
    register({
        name: 'read_deck_layout',
        title: 'Read deck layout',
        description: 'Read current key labels, paths, actions and triggers.',
        inputSchema: {
            type: 'object',
            properties: {},
            additionalProperties: false
        },
        annotations: {
            readOnlyHint: true,
            untrustedContentHint: true
        },
        execute: async () => ({
            content: [{
                type: 'text',
                text: JSON.stringify({
                    ...project,
                    keys: project.keys.map(({
                        icons,
                        deviceIcons,
                        animations,
                        deviceAnimations,
                        ...key
                    }) => ({
                        ...key,
                        customIcons: Object.keys(icons),
                        animatedStates: Object.keys(animations || {})
                    }))
                })
            }]
        })
    });
    register({
        name: 'assign_deck_key',
        title: 'Assign deck key',
        description: 'Assign one key label, action, MiSTer path and trigger in the local editor.',
        annotations: {
            readOnlyHint: false,
            untrustedContentHint: true
        },
        inputSchema: {
            type: 'object',
            properties: {
                key: {
                    type: 'integer',
                    minimum: 0,
                    maximum: 14
                },
                label: {
                    type: 'string'
                },
                action: {
                    type: 'string',
                    enum: ACTIONS
                },
                target: {
                    type: 'string'
                },
                trigger: {
                    type: 'string',
                    enum: ['press', 'release', 'hold']
                },
                hold: {
                    type: 'integer',
                    minimum: 100,
                    maximum: 10000
                }
            },
            required: ['key'],
            additionalProperties: false
        },
        execute: async args => {
            if (processing) throw Error('Wait for the image or project to finish loading.');
            if (!args || !Number.isInteger(args.key) || args.key < 0 || args.key > 14 || Object.keys(args).some(k => !['key', 'label', 'action', 'target', 'trigger', 'hold'].includes(k))) throw Error('Invalid key assignment.');
            const p = structuredClone(project),
                {
                    key,
                    ...changes
                } = args;
            Object.assign(p.keys[key], changes);
            validate(p);
            project = p;
            selected = key;
            render();
            message(`Key ${key} assigned.`);
            return {
                content: [{
                    type: 'text',
                    text: `Updated key ${key}.`
                }]
            };
        }
    });
}

window.addEventListener('pagehide', () => toolLife.abort());
render();

function validateAnimations(p) {
    let total = 0;
    for (const [i, k] of p.keys.entries()) {
        if (k.animations !== undefined && (!k.animations || typeof k.animations !== 'object' || Array.isArray(k.animations))) throw Error(`Invalid animations on key ${i}.`);
        for (const [state, a] of Object.entries(k.animations || {})) {
            if (!STATES.includes(state) || !a || !Array.isArray(a.frames) || !Array.isArray(a.durations) || a.frames.length < 1 || a.frames.length > 32 || a.frames.length !== a.durations.length || !k.icons[state]) throw Error(`Invalid animation on key ${i}.`);
            if (a.durations.some(d => !Number.isInteger(d) || d < 200 || d > 30000) || a.durations.reduce((n, d) => n + d, 0) > 30000) throw Error('Animation timings must be 200–30000 ms with a loop of at most 30 seconds.');
            let size = 8;
            for (const frame of a.frames) {
                if (typeof frame !== 'string' || !/^data:image\/(?:jpeg|png);base64,[A-Za-z0-9+/]+=*$/.test(frame) || frame.length > 43715) throw Error('Invalid animation preview.');
                const length = bytes(frame).length;
                if (length > 32768) throw Error('Invalid animation preview.');
                size += 8 + length;
            }
            if (size > 524288) throw Error('Animation exceeds 512 KiB.');
            total += size;
        }
    }
    if (total > 4194304) throw Error('Animations exceed the 4 MiB cache limit.');
}

function previewFrame(key, state, now) {
    const a = key.animations?.[state];
    if (!a) return null;
    let t = now % a.durations.reduce((n, d) => n + d, 0);
    for (let i = 0; i < a.frames.length; i++) {
        if (t < a.durations[i]) return a.frames[i];
        t -= a.durations[i];
    }
    return a.frames[0];
}
window.setInterval?.(() => {
    if (document.hidden || processing) return;
    const now = Date.now();
    $('deck').querySelectorAll('img').forEach((img, i) => {
        const frame = previewFrame(project.keys[i], 'normal', now);
        if (frame && img.src !== frame) img.src = frame;
    });
    const frame = previewFrame(project.keys[selected], $('state').value, now);
    if (frame && $('iconPreview').src !== frame) $('iconPreview').src = frame;
}, 200);

function applyMarioPreset(key) {
    applyConsolePreset(key, 0);
}
$('marioPreset').onclick = () => {
    if (processing) return;
    applyMarioPreset(project.keys[selected]);
    render();
    message('Mario Bros. animation assigned to the normal icon.');
};

function applyConsolePreset(key, index) {
    clearIconState(key, "normal");
    const preset = CONSOLE_PRESETS[index];
    key.animations ??= {};
    key.deviceAnimations ??= {};
    key.animations.normal = {
        frames: [...preset.frames],
        durations: [...preset.durations]
    };
    key.deviceAnimations.normal = [...preset.device];
    key.icons.normal = preset.frames[0];
}
$('consolePreset').onclick = () => {
    if (processing) return;
    const index = Number($('consoleChoice').value);
    if (!Number.isInteger(index) || index < 0 || index >= CONSOLE_PRESETS.length) return;
    applyConsolePreset(project.keys[selected], index);
    render();
    message(`${index === 13 ? "Stay Puft" : LABELS[index]} animation assigned to the normal icon.`);
};
