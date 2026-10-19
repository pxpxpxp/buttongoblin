'use strict';
// Test the actual editor with a lightweight DOM and a real canvas rasterizer.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const {createCanvas,Image}=require('@napi-rs/canvas');
class EditorImage extends Image {async decode(){if(this.width)return;await new Promise((resolve,reject)=>{this.onload=resolve;this.onerror=reject;});}}
const elements=new Map();
function el(id){if(!elements.has(id))elements.set(id,{value:id==='state'?'normal':'',classList:{toggle(){},remove(){}},setAttribute(){},append(...items){this.children.push(...items);},children:[],replaceChildren(){this.children=[];},querySelectorAll(type){return type==='img'?this.children.map(b=>b.children[0]):[];},click(){}});return elements.get(id);}
const document={getElementById:el,querySelectorAll:()=>[],createElement:type=>type==='canvas'?createCanvas(80,80):{...el(Symbol(type)),className:'',setAttribute(){},append(...items){this.children.push(...items);}},hidden:false};
const context=vm.createContext({document,window:{setInterval(){}},Image:EditorImage,console,Blob,TextEncoder,Uint8Array,Uint32Array,DataView,atob,structuredClone,setTimeout,URL,confirm:()=>true});
for(const name of ['mini-defaults.js','gif.js','mini-app.js'])vm.runInContext(fs.readFileSync('docs/editor/'+name,'utf8'),context,{filename:name});
const run=s=>vm.runInContext(s,context);
(async()=>{
 assert.equal(run('project.pages.length'),6);assert.equal(el('deck').children.length,6);assert.equal(run('project.pages[0].keys[3].action'),'update_all');
 run('validate(project);validateNavigation(project)');
 for(const mutation of ["p.format='buttongoblin-v1'","p.pages.pop()","p.pages[0].keys.pop()","p.pages[0].keys[0].label='bad\\nconfig=1'","p.pages[0].keys[0].animations.normal.durations[0]=1","p.pages[0].keys[0].icons.normal='javascript:alert(1)'","p.pages[0].keys[0].action='unsupported'"]){assert.throws(()=>run(`{const p=structuredClone(project);${mutation};validate(p);}`));}
 assert.throws(()=>run("{const p=structuredClone(project);p.pages[1].keys[5].action='none';validateNavigation(p)}"));
 assert.throws(()=>run("{const p=structuredClone(project);p.pages[0].keys[3].action='none';validateNavigation(p)}"));
 // Asymmetric image proves transpose, BGR channels and bottom-up row order.
 const pixels=new Uint8Array(80*80*4);for(let y=0;y<80;y++)for(let x=0;x<80;x++){const i=(y*80+x)*4;pixels.set([x,y,17,255],i);}context.testPixels=pixels;
 const bmp=run('bitmapFromRGBA(testPixels)');assert.deepEqual([...bmp.slice(54,57)],[17,0,79]);assert.deepEqual([...bmp.slice(54+79*240+3,54+79*240+6)],[17,1,0]);assert.equal(bmp.length,19254);
 assert.equal(run("miniAnimation([bitmapFromRGBA(testPixels)],[350])[1]"),77);
 // Page selection, target editing and safe fixed triggers.
 el('page').value='5';el('page').onchange();assert.equal(run('activePage'),5);assert.equal(run('key().action'),'y');assert.equal(el('trigger').disabled,true);
 el('action').value='next';el('action').onchange();assert.equal(run('key().trigger'),'release');run('project=defaults();activePage=selected=0;render()');
 // Import is transactional: invalid dimensions must preserve existing edits.
 context.badFrame=createCanvas(1,1).toDataURL('image/png');run("project.pages[0].keys[0].label='Keep my edits'");
 const invalid=JSON.parse(run('JSON.stringify(project)'));invalid.pages[0].keys[0].icons.normal=context.badFrame;invalid.pages[0].keys[0].animations={};
 el('projectFile').files=[{size:100,text:async()=>JSON.stringify(invalid)}];await el('projectFile').onchange();assert.equal(run('project.pages[0].keys[0].label'),'Keep my edits');assert.match(el('status').textContent,/80 × 80/);
 run('project=defaults();activePage=selected=0;render()');
 // Round trip all six pages, including artwork and Y/N actions.
 const saved=run('JSON.stringify(project)');el('projectFile').files=[{size:saved.length,text:async()=>saved}];await el('projectFile').onchange();assert.equal(run('project.pages[5].keys[1].action'),'n');assert.equal(el('status').textContent,'All six Mini pages opened.');
 // Replacing uploads must not retain discarded BMP frames in the derived cache.
 context.FileReader=class {readAsDataURL(f){this.result=f.data;this.onload();}};
 run('deviceCache.clear()');
 for(const color of ['red','green','blue']){
 const canvas=createCanvas(80,80),ctx=canvas.getContext('2d');ctx.fillStyle=color;ctx.fillRect(0,0,80,80);
 el('iconFile').files=[{size:100,type:'image/png',data:canvas.toDataURL('image/png')}];await el('iconFile').onchange();
 assert.equal(run('deviceCache.size'),0,'Discarded upload BMPs are not retained');
 }
 run('project=defaults();activePage=selected=0;render()');
 const files=await run('exportFiles()');const out=process.argv[2]||'/tmp/buttongoblin-mini-editor-test';fs.mkdirSync(out,{recursive:true});
 for(const [name,data] of files){const path=require('node:path').join(out,name);fs.mkdirSync(require('node:path').dirname(path),{recursive:true});fs.writeFileSync(path,data);}
 fs.writeFileSync(out+'/layout.zip',Buffer.from(await (await run('(async()=>zip(await exportFiles()))()')).arrayBuffer()));
 // Shared GIF conversion uses Mini geometry and produces real Mini BMP frames.
 const gif=Buffer.from('R0lGODdhAgACAIEAAP8AAAAAAAAAAAAAACwAAAAAAgACAAAIBgABCAQQEAA7','base64');context.gifBuffer=gif.buffer.slice(gif.byteOffset,gif.byteOffset+gif.byteLength);
 const animation=await run('normaliseGif(gifBuffer,false,"")');assert.equal(animation.device[0].length,19254);assert.equal(animation.frames[0].startsWith('data:image/png'),true);
 console.log('PASS: six-page Mini editor, atomic imports, project round trip, navigation guards, GIF conversion, BMP orientation and complete ZIP export');
})().catch(e=>{console.error(e);process.exitCode=1;});
