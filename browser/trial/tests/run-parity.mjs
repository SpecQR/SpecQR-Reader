#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import createModule from '../assets/specqr.mjs';
import {invoke, equal} from './wasm-api.mjs';
import {validateManifest, validateFixtureResults} from './coverage.mjs';
import {sourceIdentity} from './source-identity.mjs';

const trial = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const evidence = path.resolve(process.argv[3] || path.join(trial, '../evidence/wasm'));
const nativePath = process.argv[2];
if (!nativePath) throw new Error('Usage: node tests/run-parity.mjs NATIVE_BRIDGE [EVIDENCE_DIRECTORY]');
fs.mkdirSync(path.join(evidence, 'raw'), {recursive: true});
const manifest = JSON.parse(fs.readFileSync(path.join(trial, 'samples/manifest.json')));
validateManifest(manifest);
const fixtures = manifest.samples.filter(s => s.acceptance);
const oraclePath = path.join(trial, 'tests/native-expected.json');
const oracleBytes = fs.readFileSync(oraclePath);
const storedOracle = JSON.parse(oracleBytes);
validateFixtureResults(storedOracle.fixtures);
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const module = await createModule({locateFile:file => path.join(trial, 'assets', file)});
const report = {createdUtc: new Date().toISOString(), node:process.version, nativePath:path.resolve(nativePath), nativeSha256:hash(fs.readFileSync(nativePath)), wasmSha256:hash(fs.readFileSync(path.join(trial,'assets/specqr.wasm'))), source:sourceIdentity(path.resolve(trial,'../..')), oracleSha256:hash(oracleBytes), comparisonExcludes:['runtime_ms','repeat','peak_rss_kib'], fixtures:[], boundaries:[], memory:{}, assertions:[]};
function check(name, pass, detail) { report.assertions.push({name,pass,detail}); if (!pass) process.exitCode=1; }
function native(spec, data, id) {
  const rawfile = path.join(evidence,'raw',id+'.bin'); fs.writeFileSync(rawfile,data);
  const args=[spec.kind,rawfile,spec.width,spec.height,spec.stride,spec.channels,spec.flags||0,spec.maxCandidates||0,spec.maxSymbols||0,spec.maxGridAttempts||0].map(String);
  const run=spawnSync(nativePath,args,{encoding:'utf8',maxBuffer:16*1024*1024,timeout:120000});
  if(run.error)throw run.error;
  const result=JSON.parse(run.stdout);
  check(id+' native exit status',run.status===(result.ok?0:2));
  fs.writeFileSync(path.join(evidence,'raw',id+'.native.json'),JSON.stringify(result,null,2)+'\n');
  return {result,exitCode:run.status,stderr:run.stderr};
}
function matchSubset(actual, expected) {
  if (Array.isArray(expected)) return Array.isArray(actual)&&actual.length===expected.length&&expected.every((v,i)=>matchSubset(actual[i],v));
  if(expected&&typeof expected==='object')return actual&&Object.entries(expected).every(([k,v])=>matchSubset(actual[k],v));
  return actual===expected;
}
const expectedBrowser={version:1,comparisonExcludes:report.comparisonExcludes,fixtures:[]};
for(const fixture of fixtures) {
  const data=fs.readFileSync(path.join(trial,'samples',fixture.rawPath));
  check(fixture.id+' raw hash',hash(data)===fixture.rawSha256);
  for(const flags of [0,1]) {
    const id=fixture.id+(flags?'-diagnostics':'-normal');
    const spec={...fixture,flags};
    const n=native(spec,data,id); const w=invoke(module,spec,data);
    fs.writeFileSync(path.join(evidence,'raw',id+'.wasm.json'),JSON.stringify(w.result,null,2)+'\n');
    const parity=equal(n.result,w.result);
    check(id+' all JSON fields parity',parity);
    check(id+' tracked oracle unchanged',equal(n.result,storedOracle.fixtures.find(f=>f.id===fixture.id&&f.diagnostics===Boolean(flags)).result));
    const actual=w.result.symbols.slice().sort((a,b)=>a.bytes_hex.localeCompare(b.bytes_hex));
    const expected=fixture.expectedSymbols.slice().sort((a,b)=>a.bytes_hex.localeCompare(b.bytes_hex));
    const oracle=matchSubset(actual,expected);
    check(id+' independent bytes and metadata oracle',oracle);
    check(id+' input/result buffers released',w.liveInputBytes===0&&w.resultBytes===0);
    report.fixtures.push({id:fixture.id,kind:fixture.kind,diagnostics:Boolean(flags),parity,oracle,status:w.result.error,symbols:w.result.symbols.length,nativeExitCode:n.exitCode});
    expectedBrowser.fixtures.push({id:fixture.id,diagnostics:Boolean(flags),result:n.result});
  }
}
const repeated=fixtures.find(x=>x.id==='same-payload-two-gray');
const repeatData=fs.readFileSync(path.join(trial,'samples',repeated.rawPath));
const grid=fixtures.find(x=>x.id==='binary-nul-invalid-utf8');
const gridData=fs.readFileSync(path.join(trial,'samples',grid.rawPath));
const cases=[
  {id:'partial-max-symbols',spec:{...repeated,maxSymbols:1},data:repeatData,expect:{ok:true,error:'none',work_limit_reached:true,count:1}},
  {id:'work-limit-candidates',spec:{...repeated,maxCandidates:2},data:repeatData,expect:{ok:false,error:'resource_limit',work_limit_reached:true,count:0}},
  {id:'partial-max-attempts',spec:{...repeated,maxGridAttempts:1},data:repeatData,expect:{ok:true,error:'none',work_limit_reached:true,count:1}},
  {id:'zero-width',spec:{kind:'image',width:0,height:21,stride:21,channels:1},data:Buffer.alloc(441,255),expect:{ok:false,error:'invalid_input',count:0}},
  {id:'bad-channels',spec:{kind:'image',width:21,height:21,stride:84,channels:4},data:Buffer.alloc(1764,255),expect:{ok:false,error:'invalid_input',count:0}},
  {id:'short-image-buffer',spec:{kind:'image',width:21,height:21,stride:21,channels:1},data:Buffer.alloc(440,255),expect:{ok:false,error:'invalid_input',count:0}},
  {id:'short-grid-buffer',spec:{...grid},data:gridData.subarray(0,gridData.length-1),expect:{ok:false,error:'invalid_input',count:0}},
  {id:'bad-grid-size',spec:{kind:'grid',width:22,height:22,stride:22,channels:1},data:Buffer.alloc(484),expect:{ok:false,error:'invalid_input',count:0}},
  {id:'nonbinary-grid',spec:{...grid},data:Buffer.alloc(441,2),expect:{ok:false,error:'invalid_input',count:0}},
  {id:'large-valid-4mp',spec:{kind:'image',width:2048,height:2048,stride:2048,channels:1},data:Buffer.alloc(2048*2048,255),expect:{ok:false,error:'not_found',count:0}},
  {id:'over-max-dimension',spec:{kind:'image',width:8193,height:21,stride:8193,channels:1},data:Buffer.alloc(8193*21,255),expect:{ok:false,error:'resource_limit',count:0}},
  {id:'over-max-pixels',spec:{kind:'image',width:4097,height:4097,stride:4097,channels:1},data:Buffer.alloc(4097*4097,255),expect:{ok:false,error:'resource_limit',count:0}}
];
for(const c of cases) {
  const n=native(c.spec,c.data,c.id); const w=invoke(module,c.spec,c.data);
  const expected={...c.expect};delete expected.count;
  const status=matchSubset(w.result,expected)&&w.result.symbols.length===c.expect.count;
  check(c.id+' expected status',status,w.result.error);
  check(c.id+' native/Wasm parity',equal(n.result,w.result));
  check(c.id+' released',w.liveInputBytes===0&&w.resultBytes===0);
  report.boundaries.push({id:c.id,pass:status&&equal(n.result,w.result),result:w.result});
}
const warmup=[];for(let i=0;i<20;i++) warmup.push(invoke(module,repeated,repeatData).heapBytes);
const heapBefore=module.HEAPU8.buffer.byteLength;
let cleanup=true,equivalent=true;
const reference=invoke(module,repeated,repeatData).result;
for(let i=0;i<200;i++) {const r=invoke(module,repeated,repeatData);cleanup&&=r.liveInputBytes===0&&r.resultBytes===0;equivalent&&=equal(reference,r.result);}
const heapAfter=module.HEAPU8.buffer.byteLength;
report.memory={warmupRuns:20,repeatedRuns:200,heapBefore,heapAfter,buffersReleasedEveryRun:cleanup,resultsStable:equivalent,heapPlateau:heapBefore===heapAfter,note:'Wasm heap high-water capacity does not shrink; this checks stable capacity and explicit bridge buffers, not every internal allocator allocation.'};
check('200 repeat buffers released',cleanup);check('200 repeat outputs stable',equivalent);check('post-warmup heap plateau',heapBefore===heapAfter);
check('wrapper allocation >64MiB rejected',module._specqr_alloc(64*1024*1024+1)===0);
check('wrapper zero allocation rejected',module._specqr_alloc(0)===0);
validateFixtureResults(report.fixtures);
check('tracked oracle file not modified',hash(fs.readFileSync(oraclePath))===report.oracleSha256);
check('source identity unchanged during validation',sourceIdentity(path.resolve(trial,'../..')).aggregateSha256===report.source.aggregateSha256);
report.summary={fixtureRuns:report.fixtures.length,uniqueFixtures:fixtures.length,gridFixtures:fixtures.filter(f=>f.kind==='grid').length,imageFixtures:fixtures.filter(f=>f.kind==='image').length,symbolsPerLane:report.fixtures.filter(f=>!f.diagnostics).reduce((s,f)=>s+f.symbols,0),boundaryCases:report.boundaries.length,assertions:report.assertions.length,passed:report.assertions.filter(x=>x.pass).length,failed:report.assertions.filter(x=>!x.pass).length};
report.pass=report.summary.failed===0;
// This is observed output, never an implicit replacement for the tracked oracle.
fs.writeFileSync(path.join(evidence,'native-observed.json'),JSON.stringify({...expectedBrowser,createdUtc:report.createdUtc,source:report.source,nativeSha256:report.nativeSha256,wasmSha256:report.wasmSha256,pass:report.pass},null,2)+'\n');
fs.writeFileSync(path.join(evidence,'parity.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({pass:report.pass,...report.summary,memory:report.memory},null,2));
