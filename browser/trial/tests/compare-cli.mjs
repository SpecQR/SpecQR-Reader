#!/usr/bin/env node
// Compare this candidate's original CLI serializer with fresh bridge output.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {equal} from './wasm-api.mjs';
import {validateManifest, validateFixtureResults} from './coverage.mjs';
import {sha256, sourceIdentity} from './source-identity.mjs';
const [cli, observedFile, outputFile] = process.argv.slice(2);
if(!cli||!observedFile||!outputFile)throw new Error('Usage: node browser/trial/tests/compare-cli.mjs READER_CLI NATIVE_OBSERVED_JSON OUTPUT_JSON');
const trial=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const manifest=JSON.parse(fs.readFileSync(path.join(trial,'samples/manifest.json')));
validateManifest(manifest);
const observed=JSON.parse(fs.readFileSync(observedFile));
if(observed.pass!==true)throw new Error('Fresh native/Wasm comparison did not pass');
validateFixtureResults(observed.fixtures);
if(observed.source?.aggregateSha256!==sourceIdentity(path.resolve(trial,'../..')).aggregateSha256)throw new Error('Observed native/Wasm source identity differs from this candidate');
const checks=[];
for(const fixture of manifest.samples.filter(f=>f.acceptance))for(const diagnostics of [false,true]) {
  const input=path.join(trial,'samples',fixture.path);
  if(sha256(fs.readFileSync(input))!==fixture.sha256)throw new Error(fixture.id+' source fixture hash mismatch');
  const args=[fixture.kind,input,...(diagnostics?['--trace']:[])];
  const execution=spawnSync(cli,args,{encoding:'utf8',maxBuffer:16*1024*1024,timeout:120000});
  if(execution.error)throw execution.error;
  const result=JSON.parse(execution.stdout);
  const expected=observed.fixtures.find(f=>f.id===fixture.id&&f.diagnostics===diagnostics).result;
  checks.push({id:fixture.id,diagnostics,pass:execution.status===(result.ok?0:2)&&equal(result,expected),exitCode:execution.status,result});
}
validateFixtureResults(checks);
const report={createdUtc:new Date().toISOString(),node:process.version,source:sourceIdentity(path.resolve(trial,'../..')),
  cliSha256:sha256(fs.readFileSync(cli)),nativeObservedSha256:sha256(fs.readFileSync(observedFile)),
  comparisonExcludes:['runtime_ms','repeat','peak_rss_kib'],checks,
  summary:{total:checks.length,passed:checks.filter(c=>c.pass).length,failed:checks.filter(c=>!c.pass).length},pass:checks.every(c=>c.pass)};
fs.mkdirSync(path.dirname(path.resolve(outputFile)),{recursive:true});
fs.writeFileSync(outputFile,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report.summary));if(!report.pass)process.exitCode=1;
