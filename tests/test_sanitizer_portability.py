#!/usr/bin/env python3
"""Execute the sanitize recipe with controlled uname and recursive-make probe.

No compiler/runtime is simulated as having passed sanitizer tests. These are
recipe selection/error-propagation controls; actual sanitizer runs are separate.
Requires POSIX shell and make, matching the Make sanitizer target's environment.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

ROOT=pathlib.Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--makefile',type=pathlib.Path,default=ROOT/'Makefile')
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'evidence/revision-4/sanitizer-portability-controls.json')
    args=parser.parse_args()
    records=[]
    with tempfile.TemporaryDirectory(prefix='specqr-sanitizer-control-') as temporary:
        root=pathlib.Path(temporary)
        bindir=root/'bin';bindir.mkdir()
        uname=bindir/'uname'
        uname.write_text('#!/bin/sh\nprintf \'%s\\n\' "$SPECQR_TEST_PLATFORM"\n')
        uname.chmod(0o755)
        probe=root/'probe.py'
        probe.write_text('''import json,os,pathlib,sys
pathlib.Path(os.environ['SPECQR_TEST_LOG']).write_text(json.dumps({
    'argv':sys.argv[1:],'asan_options':os.environ.get('ASAN_OPTIONS'),
    'ubsan_options':os.environ.get('UBSAN_OPTIONS'),
    'exit_code':int(os.environ.get('SPECQR_TEST_CHILD_EXIT','0'))}))
raise SystemExit(int(os.environ.get('SPECQR_TEST_CHILD_EXIT','0')))
''')
        runner=bindir/'recursive-make-probe'
        runner.write_text('#!/bin/sh\nexec '+shlex.quote(sys.executable)+' '+shlex.quote(str(probe))+' "$@"\n')
        runner.chmod(0o755)
        cases=[('linux-default','Linux',None,None,0,1),
               ('darwin-default','Darwin',None,None,0,0),
               ('darwin-explicit-enable','Darwin','1',None,0,1),
               ('linux-explicit-disable','Linux','0',None,0,0),
               ('environment-override','Linux',None,'0',0,0),
               ('command-line-overrides-environment','Darwin','1','0',0,1),
               ('linux-child-error','Linux',None,None,17,1),
               ('darwin-child-error','Darwin',None,None,17,0)]
        for name,system,override,environment_override,child_exit,leaks in cases:
            log=root/(name+'.json')
            env=os.environ.copy()
            for key in ('MAKEFLAGS','MFLAGS','MAKEOVERRIDES','SANITIZER_DETECT_LEAKS'):
                env.pop(key,None)
            env.update(PATH=str(bindir)+os.pathsep+env.get('PATH',''),SPECQR_TEST_PLATFORM=system,
                       SPECQR_TEST_LOG=str(log),SPECQR_TEST_CHILD_EXIT=str(child_exit))
            if environment_override is not None:env['SANITIZER_DETECT_LEAKS']=environment_override
            command=['make','--no-print-directory','-f',str(args.makefile.resolve()),'sanitize','MAKE='+str(runner)]
            if override is not None:command.append('SANITIZER_DETECT_LEAKS='+override)
            result=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True,timeout=20)
            if not log.is_file():raise AssertionError(f'{name}: recursive command did not run: {result.stderr}')
            observed=json.loads(log.read_text())
            checks={'asan_options':observed['asan_options']==f'detect_leaks={leaks}:halt_on_error=1',
                    'ubsan_options':observed['ubsan_options']=='halt_on_error=1',
                    'separate_build_directory':'BUILD_DIR=build-make-sanitize' in observed['argv'],
                    'instrumentation':any(a.startswith('CXXFLAGS=') and '-fsanitize=address,undefined' in a for a in observed['argv']) and 'LDFLAGS=-fsanitize=address,undefined' in observed['argv'],
                    'executes_core_tests':observed['argv'][-1]=='test-core',
                    'exit_propagation':(result.returncode==0)==(child_exit==0) and observed['exit_code']==child_exit}
            record={'name':name,'mock_platform':system,'explicit_override':override,'environment_override':environment_override,
                    'make_exit_code':result.returncode,'observed_child':observed,'checks':checks,
                    'stdout':result.stdout,'stderr':result.stderr,'passed':all(checks.values())}
            records.append(record)
    evidence={'schema':'specqr-r4-sanitizer-recipe-controls-v1','makefile_sha256':hashlib.sha256(args.makefile.read_bytes()).hexdigest(),
              'test_source_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
              'cases':len(records),'passed':all(r['passed'] for r in records),'records':records,
              'scope':'Real sanitize recipe execution with uname and recursive-make probe; this does not execute a macOS sanitizer runtime.'}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps({'cases':len(records),'passed':evidence['passed'],'output':str(args.output)},indent=2))
    raise SystemExit(0 if evidence['passed'] else 1)


if __name__=='__main__':main()
