#!/usr/bin/env node
// Regression controls for acceptance gates; these are not browser execution claims.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {validateManifest,validateFixtureResults,validateCodecResults,validateBrowserReport,CODECS} from './coverage.mjs';
const manifest=JSON.parse(fs.readFileSync(new URL('../samples/manifest.json',import.meta.url)));
const oracle=JSON.parse(fs.readFileSync(new URL('./native-expected.json',import.meta.url)));
const clone=value=>structuredClone(value);
const checks=[];
function check(name,fn){try{fn();checks.push({name,pass:true});}catch(error){checks.push({name,pass:false,error:error.message});}}
check('complete manifest accepted',()=>validateManifest(manifest));
check('missing fixture rejected',()=>{const m=clone(manifest);m.samples=m.samples.filter(s=>s.id!=='metadata-sa-1');assert.throws(()=>validateManifest(m));});
check('duplicate fixture rejected',()=>{const m=clone(manifest);m.samples.push(clone(m.samples[0]));assert.throws(()=>validateManifest(m));});
check('fixture kind mismatch rejected',()=>{const m=clone(manifest);m.samples[0].kind='image';assert.throws(()=>validateManifest(m));});
check('incomplete codec manifest rejected',()=>{const m=clone(manifest);m.samples=m.samples.filter(s=>s.id!=='codec-jpeg');assert.throws(()=>validateManifest(m));});
check('complete oracle accepted',()=>validateFixtureResults(oracle.fixtures));
check('missing diagnostic lane rejected',()=>assert.throws(()=>validateFixtureResults(oracle.fixtures.slice(1))));
check('duplicate lane rejected',()=>{const r=clone(oracle.fixtures);r[1]=clone(r[0]);assert.throws(()=>validateFixtureResults(r));});
check('missing decoded symbol rejected',()=>{const r=clone(oracle.fixtures);r[0].result.symbols=[];assert.throws(()=>validateFixtureResults(r));});
check('duplicate codec output rejected',()=>assert.throws(()=>validateCodecResults([{id:'codec-png'},{id:'codec-png'},{id:'codec-png'}])));
const good={pass:true,assertions:[{pass:true}],fixtureResults:oracle.fixtures.map(f=>({...f,parity:true})),codecResults:CODECS.map(id=>({id}))};
check('complete browser report accepted',()=>validateBrowserReport(good));
check('failed browser report rejected',()=>assert.throws(()=>validateBrowserReport({...good,pass:false})));
check('failed browser assertion rejected',()=>assert.throws(()=>validateBrowserReport({...good,assertions:[{pass:false}]})));
check('failed browser fixture parity rejected',()=>{const r=clone(good);r.fixtureResults[0].parity=false;assert.throws(()=>validateBrowserReport(r));});
const report={scope:'Portable acceptance-gate controls, not actual browser execution',checks,summary:{total:checks.length,passed:checks.filter(c=>c.pass).length,failed:checks.filter(c=>!c.pass).length},pass:checks.every(c=>c.pass)};
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report.summary));if(!report.pass)process.exitCode=1;
