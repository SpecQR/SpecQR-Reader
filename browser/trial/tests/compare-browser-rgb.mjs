#!/usr/bin/env node
// Native comparison of exactly the RGB bytes exported by a real browser run.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {equal} from './wasm-api.mjs';
import {validateBrowserReport} from './coverage.mjs';
const [native, evidenceFile, outputDir] = process.argv.slice(2);
if(!native||!evidenceFile||!outputDir)throw new Error('Usage: node tests/compare-browser-rgb.mjs NATIVE_BRIDGE BROWSER_JSON OUTPUT_DIRECTORY');
const report=JSON.parse(fs.readFileSync(evidenceFile));
validateBrowserReport(report);
const currentWasm=fs.readFileSync(new URL('../assets/specqr.wasm',import.meta.url));
if(crypto.createHash('sha256').update(currentWasm).digest('hex')!==report.assetHashes?.['../assets/specqr.wasm'])throw new Error('Browser evidence Wasm differs from the current trial build');
fs.mkdirSync(outputDir,{recursive:true});
const results=[];
for(const codec of report.codecResults) {
  const bytes=Buffer.from(codec.rgbBase64,'base64');
  const sha256=crypto.createHash('sha256').update(bytes).digest('hex');
  if(!Number.isSafeInteger(codec.width)||!Number.isSafeInteger(codec.height)||codec.width<1||codec.height<1||codec.width>8192||codec.height>8192||codec.width*codec.height>16*1024*1024||codec.channels!==3||codec.stride!==codec.width*3||bytes.length!==codec.stride*codec.height||sha256!==codec.rgbSha256)throw new Error(codec.id+' RGB dimensions or integrity mismatch');
  const rawFile=path.join(outputDir,codec.id+'.rgb');fs.writeFileSync(rawFile,bytes);
  const args=['image',rawFile,codec.width,codec.height,codec.stride,codec.channels,1,0,0,0].map(String);
  const execution=spawnSync(native,args,{encoding:'utf8',maxBuffer:16*1024*1024,timeout:120000});
  if(execution.error)throw execution.error;
  const nativeResult=JSON.parse(execution.stdout);
  const pass=execution.status===(nativeResult.ok?0:2)&&equal(nativeResult,codec.wasmResult);
  fs.writeFileSync(path.join(outputDir,codec.id+'.native.json'),JSON.stringify(nativeResult,null,2)+'\n');
  results.push({id:codec.id,pass,identicalRgbSha256:sha256,rgbBytes:bytes.length,nativeExitCode:execution.status,symbols:nativeResult.symbols.length,error:nativeResult.error,transparentPixels:codec.transparentPixels});
}
const result={createdUtc:new Date().toISOString(),browserEvidence:path.resolve(evidenceFile),browserEvidenceSha256:crypto.createHash('sha256').update(fs.readFileSync(evidenceFile)).digest('hex'),userAgent:report.userAgent,assetHashes:report.assetHashes,comparisonExcludes:['runtime_ms','repeat','peak_rss_kib'],pass:results.length===3&&results.every(r=>r.pass),tests:results};
fs.writeFileSync(path.join(outputDir,'comparison.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));if(!result.pass)process.exitCode=1;
