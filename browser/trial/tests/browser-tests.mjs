import {ReaderWorkerClient, validatePayload} from '../runner.mjs';
import {decodeBrowserImage, decodePnm, decodeGrid, rgbaToRgb} from '../codec.mjs';
import {equal} from './wasm-api.mjs';
import {validateManifest, validateFixtureResults, validateCodecResults} from './coverage.mjs';

const $ = id => document.getElementById(id);
const params = new URLSearchParams(location.search);
if (params.has('label')) $('label').value = params.get('label');
let report;
const tick = () => new Promise(resolve=>setTimeout(resolve,0));
function show() {
  const visible={...report,codecResults:report.codecResults.map(({rgbBase64,...r})=>({...r,rgbBase64:`[${rgbBase64.length} base64 characters retained in saved JSON]`}))};
  $('results').textContent=JSON.stringify(visible,null,2);
}
function check(name, pass, detail) {
  report.assertions.push({name,pass:Boolean(pass),...(detail === undefined?{}:{detail})});
  $('status').textContent=`${report.assertions.length} 項目を確認中: ${name}`;
  show();
}
async function get(url,kind='json') {
  const response=await fetch(url,{cache:'no-store'});
  if(!response.ok)throw new Error(`${url}: HTTP ${response.status}`);
  return kind==='json'?response.json():kind==='text'?response.text():kind==='blob'?response.blob():response.arrayBuffer();
}
function base64(data) {
  let text='';for(let i=0;i<data.length;i+=32768)text+=String.fromCharCode(...data.subarray(i,i+32768));
  return btoa(text);
}
async function sha256(data) {return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',data)),v=>v.toString(16).padStart(2,'0')).join('');}
async function save() {
  const label=$('label').value;
  if(!/^[A-Za-z0-9_-]+$/.test(label))throw new Error('証跡ラベルは英数字、_、- のみです。');
  const response=await fetch(`/__evidence/${label}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(report)});
  if(!response.ok)throw new Error(`証跡保存 HTTP ${response.status}`);
  $('status').textContent+=` / 保存済み: ${label}`;
}
function download() {
  const label=$('label').value;
  if(!/^[A-Za-z0-9_-]+$/.test(label))throw new Error('証跡ラベルは英数字、_、- のみです。');
  const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)+'\n'],{type:'application/json'}));
  const link=document.createElement('a');link.href=url;link.download=`specqr-browser-${label}.json`;
  document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
$('save').addEventListener('click',()=>{try{download();}catch(error){$('status').textContent=error.message;}});
$('run').addEventListener('click', async()=>{
  $('run').disabled=true;$('save').disabled=true;
  report={createdUtc:new Date().toISOString(),url:location.href,userAgent:navigator.userAgent,platform:navigator.platform,browserVersionEvidence:'Exact installed browser version is recorded separately by the operator.',assertions:[],fixtureResults:[],codecResults:[],lifecycle:{}};
  const client=new ReaderWorkerClient();
  try {
    report.assetHashes={};
    for(const file of ['../assets/specqr.wasm','../assets/specqr.mjs','../runner.mjs','../codec.mjs','../renderer.mjs','../render.mjs','./native-expected.json','./browser-tests.mjs','./coverage.mjs','../samples/manifest.json'])report.assetHashes[file]=await sha256(await get(file,'bytes'));
    const [manifest,expected]=await Promise.all([get('../samples/manifest.json'),get('./native-expected.json')]);
    validateManifest(manifest);validateFixtureResults(expected.fixtures);
    check('fixed inventory: 6 grid + 6 image fixtures and 3 codec inputs',true);
    const fixtures=manifest.samples.filter(f=>f.acceptance);
    const payloads=new Map();
    for(const f of fixtures) {
      const source=await get('../samples/'+f.path,'bytes');
      check(f.id+' fixture SHA256',await sha256(source)===f.sha256);
      const payload=f.kind==='grid'?decodeGrid(new TextDecoder().decode(source)):decodePnm(source);
      payloads.set(f.id,payload);
      check(f.id+' decoded raw SHA256',await sha256(payload.data)===f.rawSha256);
      for(const diagnostics of [false,true]) {
        const out=await client.run({...payload,options:{diagnostics}});
        const reference=expected.fixtures.find(x=>x.id===f.id&&x.diagnostics===diagnostics);
        const parity=Boolean(reference)&&equal(out.result,reference.result);
        check(f.id+(diagnostics?' diagnostics':' normal')+' native parity',parity);
        check(f.id+' bridge buffers released',out.diagnostics.live_input_bytes===0&&out.diagnostics.result_bytes===0);
        report.fixtureResults.push({id:f.id,diagnostics,result:out.result,parity,metrics:out.diagnostics});
      }
      await tick();
    }
    const repeated=payloads.get('same-payload-two-gray');
    for(const [id,limits,status,count] of [['partial-symbols',{max_symbols:1},'none',1],['partial-attempts',{max_grid_attempts:1},'none',1],['candidate-work-limit',{max_candidates:2},'resource_limit',0]]) {
      const out=await client.run({...repeated,options:{limits,diagnostics:true}});
      check(id,out.result.error===status&&out.result.symbols.length===count&&out.result.work_limit_reached===true,out.result);
    }
    for(const [id,invalid] of [['zero-width',{kind:'image',width:0,height:21,stride:21,channels:1,data:new Uint8Array(441)}],['short-buffer',{kind:'image',width:21,height:21,stride:21,channels:1,data:new Uint8Array(440)}],['large-dimension',{kind:'image',width:8193,height:21,stride:8193,channels:1,data:new Uint8Array(8193*21)}],['large-pixel-count',{kind:'image',width:4097,height:4097,stride:4097,channels:1,data:new Uint8Array(1)}]]) {
      let rejected=false;try{await client.run(invalid);}catch(e){rejected=e instanceof RangeError;}
      check(id+' JS boundary rejected',rejected);
    }
    const baseline=await client.run(repeated);
    let clean=true,stable=true;const heaps=[];
    for(let i=0;i<25;i++){const out=await client.run(repeated);clean&&=out.diagnostics.live_input_bytes===0&&out.diagnostics.result_bytes===0;stable&&=equal(out.result,baseline.result);heaps.push(out.diagnostics.heap_bytes);}
    check('25 repeated runs cleanup',clean);check('25 repeated runs same results',stable);check('25 repeated runs heap plateau',heaps.every(n=>n===heaps[0]));
    report.lifecycle.repeated={runs:25,clean,stable,heapBytes:heaps,note:'Explicit bridge buffers and linear memory high-water capacity checked; not an allocator-wide leak proof.'};
    for(const sample of manifest.samples.filter(s=>s.codecTest)) {
      const file=await get('../samples/'+sample.path,'blob');
      check(sample.id+' encoded file SHA256',await sha256(await file.arrayBuffer())===sample.sha256);
      const decoded=await decodeBrowserImage(file);
      const out=await client.run({...decoded,options:{diagnostics:true}});
      check(sample.id+' decoded one expected raw payload',out.result.symbols.length===1&&out.result.symbols[0].bytes_hex==='00ff014c0d');
      check(sample.id+' no resizing',decoded.width===decoded.headerWidth&&decoded.height===decoded.headerHeight);
      if(sample.id!=='codec-jpeg')check(sample.id+' exact RGB pixels',await sha256(decoded.data)===manifest.samples.find(s=>s.id==='single-binary-rgb').rawSha256);
      if(sample.id==='codec-transparent-png')check('transparency white composite recorded',decoded.transparentPixels>0);
      report.codecResults.push({id:sample.id,format:decoded.format,codec:decoded.codec,width:decoded.width,height:decoded.height,stride:decoded.stride,channels:decoded.channels,headerWidth:decoded.headerWidth,headerHeight:decoded.headerHeight,transparentPixels:decoded.transparentPixels,rgbSha256:await sha256(decoded.data),rgbBase64:base64(decoded.data),wasmResult:out.result,metrics:out.diagnostics});
    }
    const composed=rgbaToRgb(new Uint8Array([255,0,0,0,0,0,0,128,9,8,7,255]),3,1);
    check('alpha exact integer white composite',Array.from(composed.data).join(',')==='255,255,255,127,127,127,9,8,7'&&composed.transparentPixels===2);
    let reachedRunning=false;
    client.onState=({state})=>{if(state==='running')reachedRunning=true;};
    const noisy=new Uint8Array(4096*4096);for(let i=0;i<noisy.length;i++)noisy[i]=(i*73+(i>>>10)*91)&255;
    const began=performance.now();
    const pending=client.run({kind:'image',data:noisy,width:4096,height:4096,stride:4096,channels:1}).then(()=>({resolved:true}),e=>({error:e.name,message:e.message}));
    await tick();client.stop();
    const cancellation=await pending;
    check('active worker cancellation',reachedRunning&&cancellation.error==='AbortError'&&client.worker===null,cancellation);
    report.lifecycle.cancellation={...cancellation,reachedRunning,mainThreadElapsedMs:performance.now()-began};
    const recovered=await client.run(repeated);
    check('decode after cancellation',equal(recovered.result,baseline.result));
    const faulty=new ReaderWorkerClient({workerURL:new URL('./crash-worker.js',import.meta.url),startupTimeout:5000});
    try {
      let failed=false,message='';try{await faulty.run(repeated);}catch(e){failed=true;message=e.message;}
      check('worker failure surfaced',failed&&message.includes('intentional-test-worker-failure'),message);
      check('failed worker disposed',faulty.worker===null&&faulty.pending===null);
      faulty.workerURL=new URL('../worker.js',import.meta.url);
      const restarted=await faulty.run(repeated);
      check('decode after worker failure',equal(restarted.result,baseline.result));
      report.lifecycle.workerFailure={failed,message,recovered:equal(restarted.result,baseline.result)};
    }finally{faulty.dispose();}
    // This shared renderer check is installed by the UI implementation.
    try {
      const {testUntrustedRendering}=await import('../render.mjs');
      const rendering=testUntrustedRendering();check('untrusted payload renders as inert text',rendering.pass,rendering);
    } catch(error) {check('shared untrusted rendering test available',false,error.message);}
  }catch(error){check('test harness completed',false,error.stack||error.message);}
  finally {
    client.dispose();
    try {validateFixtureResults(report.fixtureResults);validateCodecResults(report.codecResults);check('complete coverage: 24 lanes, 14 symbols/lane, 3 distinct codecs',true);}
    catch(error){check('complete acceptance coverage',false,error.message);}
    report.finishedUtc=new Date().toISOString();report.pass=report.assertions.every(a=>a.pass);
    report.summary={assertions:report.assertions.length,passed:report.assertions.filter(a=>a.pass).length,failed:report.assertions.filter(a=>!a.pass).length,fixtureRuns:report.fixtureResults.length,symbolsPerLane:report.fixtureResults.filter(f=>!f.diagnostics).reduce((sum,f)=>sum+f.result.symbols.length,0),codecInputs:report.codecResults.length};
    show();$('status').textContent=`${report.pass?'PASS':'FAIL'}: ${report.summary.passed}/${report.summary.assertions} 項目、fixture ${report.summary.fixtureRuns} 実行`;
    $('status').className=report.pass?'pass':'fail';$('run').disabled=false;$('save').disabled=false;
    if(params.get('collector')==='1') {
      try{await save();}catch(error){$('status').textContent+=' / サーバー保存できません。JSON をダウンロードしてください: '+error.message;}
    }
  }
});
