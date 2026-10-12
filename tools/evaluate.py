#!/usr/bin/env python3
"""Fixture recall, false-decode, metadata and resource evidence.

Usage: python3 tools/evaluate.py --binary build/specqr-reader
Optional test refs: references/python/{segno,qrcode,zxingcpp}, NumPy, Pillow.
Reported core timing excludes adapter loading and process startup. Product RSS
is Linux VmHWM after core decoding; launcher-influenced wait4 RSS stays separate.
"""
from __future__ import annotations
import argparse, collections, hashlib, json, os, pathlib, platform, re
import statistics, subprocess, sys, time, shutil, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'references/python'))


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def cpu_metadata():
    """Best-effort descriptive metadata; never required for an evaluation."""
    if sys.platform.startswith('linux'):
        try:
            lines=pathlib.Path('/proc/cpuinfo').read_text(encoding='utf-8',errors='replace').splitlines()
            for line in lines:
                key,separator,value=line.partition(':')
                if separator and key.strip()=='model name' and value.strip():
                    return value.strip()
        except OSError:
            pass
    try:
        return platform.processor().strip() or None
    except OSError:
        return None


def measured_run(command, timeout):
    """wait4 supplies this exact child's high water RSS, not cumulative child RSS."""
    if not hasattr(os,'wait4'):
        return subprocess.run(command,capture_output=True,text=True,timeout=timeout),None
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        child=subprocess.Popen(command,stdout=out,stderr=err,stdin=subprocess.DEVNULL)
        deadline=time.monotonic()+timeout
        while True:
            pid,status,usage=os.wait4(child.pid,os.WNOHANG)
            if pid:
                child.returncode=os.waitstatus_to_exitcode(status)
                break
            if time.monotonic()>=deadline:
                child.kill()
                _,status,_=os.wait4(child.pid,0)
                child.returncode=os.waitstatus_to_exitcode(status)
                raise subprocess.TimeoutExpired(command,timeout)
            time.sleep(.001)
        out.seek(0);err.seek(0)
        completed=subprocess.CompletedProcess(command,child.returncode,out.read().decode(),err.read().decode())
        # Linux ru_maxrss is KiB; macOS reports bytes.
        rss=usage.ru_maxrss/(1024 if sys.platform=='darwin' else 1)
        return completed,rss


def subset_equal(actual, expected):
    if isinstance(expected,dict):
        return isinstance(actual,dict) and all(k in actual and subset_equal(actual[k],v) for k,v in expected.items())
    if isinstance(expected,list):
        return isinstance(actual,list) and len(actual)==len(expected) and all(subset_equal(a,e) for a,e in zip(actual,expected))
    return actual == expected


def compare(got, expected, policy):
    gc, ec = collections.Counter(got), collections.Counter(expected)
    correct = sum((gc & ec).values())
    false = sum((gc - ec).values())
    passed = gc == ec if policy == 'exact' else false == 0
    return passed,correct,false


def cli_status_matches(returncode, result):
    """Validate the CLI's exit/JSON contract independently of payload matching.

    A partial image search that found symbols is still a successful result;
    work_limit_reached describes completeness, not Error::resource_limit.
    """
    errors={'invalid_input','resource_limit','not_found','format','version','ecc','payload','unsupported'}
    if not isinstance(result,dict) or type(result.get('ok')) is not bool:
        return False
    symbols=result.get('symbols')
    if not isinstance(symbols,list) or type(result.get('work_limit_reached',False)) is not bool:
        return False
    if result['ok']:
        return returncode==0 and result.get('error')=='none' and bool(symbols)
    return returncode==2 and result.get('error') in errors and not symbols


def product_source_hashes():
    paths=sorted(set((ROOT/'src').glob('*')) | set((ROOT/'include/specqr').glob('*')) | {ROOT/'tools/reader_cli.cpp'})
    return {str(p.relative_to(ROOT)):sha(p) for p in paths if p.is_file()}


def gate_failures(records, changed, reference_enabled):
    failures=[]
    for row in records:
        if row['status']!='passed':
            failures.append({'kind':'product','vectorId':row['vectorId'],'status':row['status']})
        if reference_enabled and row.get('reference',{}).get('status')!='passed':
            failures.append({'kind':'reference','vectorId':row['vectorId'],
                             'status':row.get('reference',{}).get('status','missing')})
    failures.extend({'kind':'source_changed','path':name} for name in changed)
    return failures


def reference_decode(vector):
    import numpy as np
    import zxingcpp
    from PIL import Image
    path = ROOT/vector['input']['path']
    is_grid = vector['operation']=='reader.decode-grid'
    pure = is_grid and vector['category'] not in ('grid-orientation','grid-inversion')
    if is_grid:
        lines = path.read_text().splitlines(); n=int(lines[0])
        ary = np.asarray([[int(c) for c in r] for r in lines[1:]], dtype='uint8')
        assert ary.shape==(n,n)
        # An opt-in inverted module grid has a dark external quiet zone.
        border=1 if vector['category']=='grid-inversion' else 0
        ary = np.repeat(np.repeat(255-np.pad(ary,4,constant_values=border)*255,3,axis=0),3,axis=1)
    else:
        ary = np.asarray(Image.open(path).convert('L'))
    start=time.perf_counter()
    if pure:
        result=zxingcpp.read_barcode(ary,formats=zxingcpp.BarcodeFormat.QRCode,is_pure=True)
        results=[result] if result and result.valid else []
    else:
        results=[r for r in zxingcpp.read_barcodes(ary,formats=zxingcpp.BarcodeFormat.QRCode) if r.valid]
    duration=(time.perf_counter()-start)*1000
    return [r.bytes.hex() for r in results],duration


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',default='build/specqr-reader')
    p.add_argument('--manifest',default='fixtures/manifest.json')
    p.add_argument('--output',default='evidence/evaluation.json')
    p.add_argument('--category',action='append',default=[])
    p.add_argument('--no-reference',action='store_true')
    p.add_argument('--repeat',type=int,default=1)
    p.add_argument('--timeout',type=float,default=15)
    p.add_argument('--fail-on-mismatch',action='store_true',help='Exit 1 on product/reference failure or error, inconsistent CLI status, or observed source mutation.')
    a=p.parse_args()
    evaluator_hash=sha(pathlib.Path(__file__))
    binary=(ROOT/a.binary).resolve(); manifest=ROOT/a.manifest
    binary_hash=sha(binary)
    snapshot=ROOT/'evidence/binaries'/binary_hash/'specqr-reader'
    snapshot.parent.mkdir(parents=True,exist_ok=True)
    if not snapshot.exists():
        shutil.copyfile(binary,snapshot)
        snapshot.chmod(0o755)
    if sha(snapshot)!=binary_hash: raise SystemExit('binary snapshot hash mismatch')
    source_hashes=product_source_hashes()
    manifest_hash=sha(manifest)
    suite=json.loads(manifest.read_text())
    vectors=[v for v in suite['vectors'] if not a.category or v['category'] in a.category]
    if not vectors: raise SystemExit('no matching vectors')
    records=[]
    for i,v in enumerate(vectors):
        path=ROOT/v['input']['path']
        if sha(path)!=v['input']['sha256']: raise SystemExit('fixture hash changed: '+str(path))
        kind='grid' if v['operation']=='reader.decode-grid' else 'image'
        command=[str(snapshot),kind,str(path),'--repeat',str(a.repeat)]
        if v['options'].get('invert'): command.append('--invert')
        rec={'vectorId':v['id'],'adapterId':'specqr-reader-cpp17', 'category':v['category'],
             'expected_symbols':len(v['expect']['decode']['binaryHex']), 'checks':[]}
        start=time.perf_counter()
        try:
            proc,rss=measured_run(command,a.timeout)
            rec['process_wall_ms']=(time.perf_counter()-start)*1000
            result=json.loads(proc.stdout)
            got=[s['bytes_hex'] for s in result.get('symbols',[])]
            cli_ok=cli_status_matches(proc.returncode,result)
            expected=v['expect']['decode']['binaryHex']
            passed,correct,false=compare(got,expected,v['expect']['policy'])
            metadata=v['expect'].get('metadata')
            metadata_ok=metadata is None or (len(result['symbols'])==1 and subset_equal(result['symbols'][0],metadata))
            # Allowed rejection beyond capacity has no metadata obligation.
            rec.update(status='passed' if passed and metadata_ok and cli_ok else 'failed',
                       matched_symbols=correct,false_decodes=false,actual_symbols=len(got),
                       cli_returncode=proc.returncode,cli_ok=result.get('ok'),
                       error=result.get('error'),runtime_ms=result.get('runtime_ms'),
                       work_limit_reached=result.get('work_limit_reached',False),
                       partial_search_success=cli_ok and bool(got) and result.get('work_limit_reached',False),
                       details={'decoded':{'binaryHex':got},'message':result.get('message','')})
            rec['checks'].append({'name':'payload-multiset','status':'passed' if passed else 'failed'})
            rec['checks'].append({'name':'cli-status-contract','status':'passed' if cli_ok else 'failed'})
            if metadata is not None:
                rec['checks'].append({'name':'metadata','status':'passed' if metadata_ok else 'failed'})
                if not metadata_ok: rec['details']['metadata_actual']=result.get('symbols')
            if rss is not None: rec['launcher_influenced_wait4_peak_rss_kib']=rss
            if result.get('peak_rss_kib') is not None: rec['peak_rss_kib']=result['peak_rss_kib']
            if proc.returncode not in (0,2): rec['details']['stderr']=proc.stderr[-2000:]
        except subprocess.TimeoutExpired:
            rec.update(status='error',error='timeout',matched_symbols=0,false_decodes=0)
        except (ValueError,KeyError,OSError,TypeError,AttributeError) as e:
            rec.update(status='error',error=str(e),matched_symbols=0,false_decodes=0)
        if not a.no_reference:
            try:
                got,ms=reference_decode(v)
                reference_expect=v['expect'].get('referenceDecode',v['expect']['decode'])
                reference_expected=reference_expect['binaryHex']
                passed,correct,false=compare(got,reference_expected,v['expect']['policy'])
                rec['reference']={'adapterId':'zxing-cpp-2.3.0','status':'passed' if passed else 'failed',
                                  'binaryHex':got,'matched_symbols':correct,'false_decodes':false,'runtime_ms':ms,
                                  'expectedBinaryHex':reference_expected,'expected_symbols':len(reference_expected),
                                  'expectation_field':'expect.referenceDecode.binaryHex' if 'referenceDecode' in v['expect'] else 'expect.decode.binaryHex',
                                  'expectation_note':reference_expect.get('notes','No automatic normalization or text conversion.'),
                                  'note':'SA metadata and ECI segment details are not compared by this adapter.'}
            except Exception as e:
                rec['reference']={'status':'error','error':str(e)}
        records.append(rec)
        if (i+1)%100==0: print(f'{i+1}/{len(vectors)}',flush=True)
    categories={}
    for category in sorted({v['category'] for v in vectors}):
        rows=[r for r in records if r['category']==category]
        runtimes=[r['runtime_ms'] for r in rows if r.get('runtime_ms') is not None]
        rss=[r['peak_rss_kib'] for r in rows if 'peak_rss_kib' in r]
        expected=sum(r['expected_symbols'] for r in rows); matched=sum(r['matched_symbols'] for r in rows)
        item={'cases':len(rows),'passed':sum(r['status']=='passed' for r in rows),
              'failed':sum(r['status']=='failed' for r in rows),'errors':sum(r['status']=='error' for r in rows),
              'expected_symbols':expected,'matched_symbols':matched,'recall':matched/expected if expected else None,
              'false_decodes':sum(r['false_decodes'] for r in rows),
              'work_limit_reached':sum(r.get('work_limit_reached',False) for r in rows),
              'partial_search_successes':sum(r.get('partial_search_success',False) for r in rows),
              'runtime_ms':{'median':statistics.median(runtimes),'p95':sorted(runtimes)[min(len(runtimes)-1,int(.95*len(runtimes)))],
                            'max':max(runtimes)} if runtimes else None,
              'peak_rss_kib':{'median':statistics.median(rss),'max':max(rss)} if rss else None}
        refs=[r['reference'] for r in rows if 'reference' in r]
        if refs:
            rm=sum(r.get('matched_symbols',0) for r in refs)
            reference_expected=sum(row['reference'].get('expected_symbols',row['expected_symbols'])
                                   for row in rows if 'reference' in row)
            item['reference']={'cases':len(refs),'passed':sum(r['status']=='passed' for r in refs),
                               'failed':sum(r['status']=='failed' for r in refs),'errors':sum(r['status']=='error' for r in refs),'matched_symbols':rm,
                               'expected_symbols':reference_expected,'recall':rm/reference_expected if reference_expected else None,
                               'false_decodes':sum(r.get('false_decodes',0) for r in refs)}
        categories[category]=item
    wheels={p.name:sha(p) for p in sorted((ROOT/'references').glob('*.whl'))}
    current_source_hashes=product_source_hashes()
    changed=sorted(name for name in source_hashes.keys() | current_source_hashes.keys()
                   if source_hashes.get(name)!=current_source_hashes.get(name))
    failures=gate_failures(records,changed,not a.no_reference)
    output={'schema':'specqr-reader-evaluation-v1','suiteId':suite['id'],'manifest_sha256':manifest_hash,
            'evaluator_sha256':evaluator_hash,
            'binary':str(binary.relative_to(ROOT)) if binary.is_relative_to(ROOT) else str(binary),'binary_sha256':binary_hash,
            'binary_snapshot':str(snapshot.relative_to(ROOT)),'source_changed_during_run':changed,
            'gate':{'passed':not failures,'reference_enabled':not a.no_reference,'failures':failures},
            'environment':{'platform':platform.platform(),'machine':platform.machine(),'python':sys.version,
                           'cpu':cpu_metadata()},
            'reference_wheel_sha256':wheels,'source_sha256':source_hashes,'repeat':a.repeat,
            'methodology':{'runtime':'steady_clock in core API, arithmetic mean of --repeat N; no process/adapter load',
                           'memory':'CLI /proc/self/status VmHWM KiB sampled after core and before JSON on Linux; includes adapter/executable and is not isolated core heap. wait4 launcher-influenced high-water RSS is retained separately, not aggregated as product RSS.',
                           'recall':'exact source-byte multiset intersection / expected symbol count; multiplicity retained',
                           'false_decode':'output byte instances exceeding expected byte multiset; rejects are not false decodes',
                           'out_of_radius':'recover-or-reject cases: recall descriptive only; no guarantee of reject outside RS radius',
                           'gate':'Product byte/metadata and CLI exit/JSON status checks, enabled reference comparisons, and before/after observed source hashes must all pass.',
                           'partial_search':'Correct symbols with exit 0, ok=true, error=none and work_limit_reached=true are successful partial searches, counted separately; discovery completeness is not claimed.',
                           'reference_bytes':'Reference defaults to the exact Reader expectation. A fixture may explicitly supply expect.referenceDecode.binaryHex for a documented API representation difference; no bytes are normalized by this harness.',
                           'scope':'Synthetic deterministic distributions plus optional explicitly labeled held-out sources. No statistical real-world recall claim.'},
            'categories':categories,'records':records}
    path=ROOT/a.output;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps({'output':str(path),'sha256':sha(path),'cases':len(records),
                      'passed':sum(r['status']=='passed' for r in records),'failed':sum(r['status']=='failed' for r in records),
                      'errors':sum(r['status']=='error' for r in records),'false_decodes':sum(r['false_decodes'] for r in records),
                      'gate_passed':not failures,'gate_failure_count':len(failures)},indent=2))
    if a.fail_on_mismatch and failures:
        raise SystemExit(1)


if __name__=='__main__':main()
