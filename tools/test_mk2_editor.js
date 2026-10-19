'use strict';
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const {createCanvas,Image}=require('@napi-rs/canvas');
class EditorImage extends Image {async decode(){if(this.width)return;await new Promise((resolve,reject)=>{this.onload=resolve;this.onerror=reject;});}}
const nodes=new Map();
function node(){return {value:'normal',classList:{toggle(){}},addEventListener(){},setAttribute(){},append(){},replaceChildren(){},querySelectorAll(){return []},click(){}};}
const context=vm.createContext({TextEncoder,Uint8Array,Uint32Array,DataView,Blob,AbortController,structuredClone,atob,btoa,setTimeout,URL,Image:EditorImage,
 document:{getElementById(id){if(!nodes.has(id))nodes.set(id,node());return nodes.get(id);},createElement:tag=>tag==='canvas'?createCanvas(72,72):node(),querySelectorAll:()=>[]},window:{addEventListener(){},setInterval(){}}});
for(const file of ['builtin-icons.js','console-presets.js','update-all-preset.js','shortcut-presets.js','gif.js','app.js'])vm.runInContext(fs.readFileSync('docs/editor/'+file,'utf8'),context,{filename:file});
const run=code=>vm.runInContext(code,context);
(async()=>{
 run('validate(project)');assert.equal(run('pageProjects.length'),2);
 run('selectPage(1)');assert.equal(run('project.keys[8].action'),'right');assert.equal(run('project.keys[10].action'),'space');assert.equal(run('project.keys[14].action'),'back');
 assert(run('configText()').includes('custom/page2_8_normal.sda'));
 const saved=run('completeProject()');assert.equal(saved.shortcuts.keys.length,15);assert(!saved.keys[0].deviceAnimations);
 context.saved=saved;const hydrated=await run('hydrate(saved)');assert.equal(hydrated.shortcuts.keys.length,15);assert(hydrated.keys[0].deviceAnimations.normal.length>1);
 run('selectPage(0)');
 const count=run('exportFiles().length');assert(count>90);
 run("pageProjects[1].keys[14].action='none'");assert.throws(()=>run('exportFiles()'),/Back button/);run("pageProjects[1].keys[14].action='back'");
 run("pageProjects[0].keys[14].action='none'");assert.throws(()=>run('exportFiles()'),/brightness cycle/);run("pageProjects[0].keys[14].action='cycle'");
 run("BUILTINS['unused.sda']='data:image/jpeg;base64,'+btoa('x'.repeat(4500000))");assert.equal(run('exportFiles().length'),count);
 const out=process.argv[2]||'/tmp/buttongoblin-mk2-editor-test';
 for(const [name,data] of run('exportFiles()')){const p=path.join(out,name);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,data);}
 // The shared GIF decoder still creates 72px JPEG frames for MK.2.
 const gif=Buffer.from('R0lGODdhAgACAIEAAP8AAAAAAAAAAAAAACwAAAAAAgACAAAIBgABCAQQEAA7','base64');context.gifInput=gif.buffer.slice(gif.byteOffset,gif.byteOffset+gif.byteLength);
 const animation=await run('normaliseGif(gifInput,false,"")');assert(animation.frames[0].startsWith('data:image/jpeg'));
 context.frame=animation.frames[0];const frameImage=new EditorImage();frameImage.src=animation.frames[0];await frameImage.decode();assert.equal(frameImage.width,72);assert.equal(frameImage.height,72);
 run("BUILTINS['13_normal.jpg']=BUILTINS['unused.sda']");assert.throws(()=>run('exportFiles()'),/cache exceeds/);
 console.log('PASS: MK.2 two-page exports, animated project hydration, navigation guards, unused asset exclusion, cache limit and shared 72px GIF conversion');
})().catch(e=>{console.error(e);process.exitCode=1;});
