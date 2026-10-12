#!/usr/bin/env python3
"""Offline build/test/install/relocated-consumer commands used by CI (Python 3.8+)."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True, help='new directory outside the source tree')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    args = parser.parse_args()
    work = args.work_dir.resolve()
    if work == ROOT or ROOT in work.parents or work.exists():
        parser.error('--work-dir must be a new directory outside the source tree')
    work.mkdir(parents=True)
    records = []
    evidence = {'platform': platform.platform(), 'python': sys.version, 'commands': records, 'passed': False}

    def run(label, command, cwd=ROOT):
        started = time.monotonic()
        result = subprocess.run([str(x) for x in command], cwd=cwd, text=True, capture_output=True, timeout=600)
        (work/(label+'.stdout')).write_text(result.stdout)
        (work/(label+'.stderr')).write_text(result.stderr)
        records.append({'label':label, 'argv':[str(x) for x in command], 'cwd':str(cwd),
                        'exit_code':result.returncode, 'seconds':time.monotonic()-started})
        print(label, 'PASS' if result.returncode==0 else 'FAIL', flush=True)
        if result.returncode:
            sys.stderr.write(result.stdout+result.stderr)
            raise RuntimeError(label+' failed')

    try:
        run('cmake-version',[args.cmake,'--version'])
        run('ctest-version',[args.ctest,'--version'])
        run('configure',[args.cmake,'-S',ROOT,'-B',work/'build','-DCMAKE_BUILD_TYPE=Release',
                         '-DBUILD_SHARED_LIBS=OFF','-DSPECQR_BUILD_TESTS=ON'])
        run('build',[args.cmake,'--build',work/'build','--config','Release','--parallel','2'])
        # -C works for both single- and multi-configuration generators.
        run('ctest',[args.ctest,'-C','Release','--output-on-failure','--timeout','120'],cwd=work/'build')
        run('evaluator-controls',[sys.executable,'-B',ROOT/'tests/evaluator_gate_tests.py','--output',work/'evaluator-gate.json'])
        run('install',[args.cmake,'--install',work/'build','--config','Release','--prefix',work/'installed'])
        (work/'installed').rename(work/'relocated')
        detached=work/'detached-consumer'
        shutil.copytree(ROOT/'tests/consumer',detached)
        shutil.copyfile(ROOT/'fixtures/grids/segno-v01-H-m0.grid',detached/'sample.grid')
        run('consumer-configure',[args.cmake,'-S',detached,'-B',work/'consumer-build','-DCMAKE_BUILD_TYPE=Release',
                                 '-DCMAKE_PREFIX_PATH='+str(work/'relocated'),'-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
                                 '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF'],cwd=detached)
        run('consumer-build',[args.cmake,'--build',work/'consumer-build','--config','Release','--parallel','2'],cwd=detached)
        run('consumer-test',[args.ctest,'-C','Release','--output-on-failure','--timeout','120'],cwd=work/'consumer-build')
        evidence['installed_files']=[{'path':p.relative_to(work/'relocated').as_posix(),
                                     'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
                                    for p in sorted((work/'relocated').rglob('*')) if p.is_file()]
        evidence['passed']=True
    finally:
        (work/'result.json').write_text(json.dumps(evidence,indent=2)+'\n')
    return 0


if __name__=='__main__':
    sys.exit(main())
