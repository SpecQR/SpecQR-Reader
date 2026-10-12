#!/usr/bin/env python3
"""Exercise evaluator process gates with controlled CLI/reference adapters.

The tested main() still snapshots a fake executable, verifies fixture/source
hashes, writes a report and raises its real CLI exit status. Only the actual
decoder subprocess and optional reference call are replaced. Every temporary
file stays outside the original candidate.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--evaluator',type=pathlib.Path,default=ROOT/'tools/evaluate.py')
parser.add_argument('--output',type=pathlib.Path,default=ROOT/'evidence/revision-3/evaluator-gate-controls.json')
ARGS=parser.parse_args()
spec=importlib.util.spec_from_file_location('specqr_evaluator_under_test',ARGS.evaluator)
EVALUATOR=importlib.util.module_from_spec(spec)
# Prevent even a read-only audit run against the original from writing __pycache__.
sys.dont_write_bytecode=True
spec.loader.exec_module(EVALUATOR)
CONTROLS=[]
METADATA_EXCEPTION_CONTROLS=[]


class EvaluatorGateTests(unittest.TestCase):
    def run_case(self,name,*,returncode=0,ok=True,error='none',symbols=('41',),
                 work_limit=False,expected=('41',),policy='exact',reference=('41',),
                 reference_error=None,mutation=None,gate=True,no_reference=False,reference_expected=None):
        with tempfile.TemporaryDirectory(prefix='specqr-evaluator-control-') as directory:
            root=pathlib.Path(directory)
            for path in ('src','include/specqr','tools','fixtures','build'):
                (root/path).mkdir(parents=True)
            (root/'build/fake-reader').write_bytes(b'controlled executable; subprocess is stubbed\n')
            source=root/'src/sentinel.cpp';source.write_text('before\n')
            (root/'tools/reader_cli.cpp').write_text('controlled CLI source\n')
            fixture=root/'fixtures/control.grid';fixture.write_text('test transport only\n')
            manifest={'id':'evaluator-negative-controls','vectors':[{
                'id':name,'category':'gate-controls','operation':'reader.decode-grid',
                'input':{'path':'fixtures/control.grid','sha256':hashlib.sha256(fixture.read_bytes()).hexdigest()},
                'options':{},'expect':{'decode':{'binaryHex':list(expected)},'policy':policy}}]}
            if reference_expected is not None:
                manifest['vectors'][0]['expect']['referenceDecode']={
                    'binaryHex':list(reference_expected),'notes':'Explicit adapter representation control; no automatic conversion.'}
            (root/'fixtures/manifest.json').write_text(json.dumps(manifest))
            response={'ok':ok,'error':error,'message':'controlled CLI response','runtime_ms':0.125,
                      'peak_rss_kib':1024,'work_limit_reached':work_limit,
                      'symbols':[{'bytes_hex':s} for s in symbols]}

            def fake_process(command,timeout):
                self.assertTrue(pathlib.Path(command[0]).is_file(),'main must snapshot the binary')
                if mutation=='modify':source.write_text('after\n')
                elif mutation=='delete':source.unlink()
                elif mutation=='add':(root/'src/added.cpp').write_text('new source\n')
                return subprocess.CompletedProcess(command,returncode,json.dumps(response),''),2048

            def fake_reference(vector):
                if reference_error is not None:raise reference_error
                return list(reference),0.25

            argv=['evaluate.py','--binary','build/fake-reader','--output','evidence/control.json']
            if gate:argv.append('--fail-on-mismatch')
            if no_reference:argv.append('--no-reference')
            status=0
            with mock.patch.object(EVALUATOR,'ROOT',root),mock.patch.object(EVALUATOR,'measured_run',fake_process), \
                 mock.patch.object(EVALUATOR,'reference_decode',fake_reference),mock.patch.object(sys,'argv',argv), \
                 contextlib.redirect_stdout(io.StringIO()):
                try:EVALUATOR.main()
                except SystemExit as e:status=e.code
            report=json.loads((root/'evidence/control.json').read_text())
            CONTROLS.append({'name':name,'exit_code':status,'gate':report.get('gate'),
                             'environment':report['environment'],
                             'source_changed_during_run':report['source_changed_during_run'],
                             'record':report['records'][0],'category':report['categories']['gate-controls']})
            return status,report

    def assert_rejected(self,status,report,kind):
        self.assertEqual(status,1)
        self.assertFalse(report['gate']['passed'])
        self.assertIn(kind,[f['kind'] for f in report['gate']['failures']])

    def test_positive_success(self):
        status,report=self.run_case('positive-success')
        self.assertEqual(status,0);self.assertTrue(report['gate']['passed'])

    def test_reference_payload_mismatch(self):
        status,report=self.run_case('reference-wrong-bytes',reference=('42',))
        self.assertEqual(report['records'][0]['status'],'passed','product must pass independently')
        self.assertEqual(report['records'][0]['reference']['status'],'failed')
        self.assert_rejected(status,report,'reference')

    def test_reference_exception(self):
        status,report=self.run_case('reference-exception',reference_error=RuntimeError('controlled reference exception'))
        self.assertEqual(report['records'][0]['status'],'passed')
        self.assertEqual(report['records'][0]['reference']['status'],'error')
        self.assert_rejected(status,report,'reference')

    def test_reference_missing_dependency(self):
        status,report=self.run_case('reference-missing-dependency',reference_error=ImportError('controlled unavailable decoder'))
        self.assert_rejected(status,report,'reference')

    def test_positive_exit_two(self):
        status,report=self.run_case('positive-correct-bytes-exit2',returncode=2)
        checks={x['name']:x['status'] for x in report['records'][0]['checks']}
        self.assertEqual(checks['payload-multiset'],'passed','exit code must independently fail the gate')
        self.assertEqual(checks['cli-status-contract'],'failed')
        self.assert_rejected(status,report,'product')

    def test_positive_error_fields(self):
        for name,kwargs in [('positive-ok-false',{'ok':False}),
                            ('positive-error-payload',{'error':'payload'}),
                            ('positive-error-plus-symbol',{'returncode':2,'ok':False,'error':'ecc'}),
                            ('positive-unknown-exit',{'returncode':7})]:
            with self.subTest(name=name):
                status,report=self.run_case(name,**kwargs)
                self.assert_rejected(status,report,'product')

    def test_successful_partial_search(self):
        status,report=self.run_case('successful-partial-search',work_limit=True)
        self.assertEqual(status,0);self.assertTrue(report['gate']['passed'])
        self.assertTrue(report['records'][0]['partial_search_success'])
        self.assertEqual(report['categories']['gate-controls']['partial_search_successes'],1)

    def test_valid_negative_rejection(self):
        status,report=self.run_case('valid-negative-rejection',returncode=2,ok=False,error='not_found',
                                    symbols=(),expected=(),reference=())
        self.assertEqual(status,0);self.assertTrue(report['gate']['passed'])

    def test_valid_recover_or_reject_control(self):
        status,report=self.run_case('valid-ecc-rejection',returncode=2,ok=False,error='ecc',
                                    symbols=(),policy='recover_or_reject',reference=())
        self.assertEqual(status,0);self.assertTrue(report['gate']['passed'])

    def test_invalid_negative_status(self):
        status,report=self.run_case('negative-inconsistent-exit',returncode=0,ok=False,error='not_found',
                                    symbols=(),expected=(),reference=())
        self.assert_rejected(status,report,'product')

    def test_source_changes(self):
        for mutation in ('modify','delete','add'):
            with self.subTest(mutation=mutation):
                status,report=self.run_case('source-'+mutation,mutation=mutation)
                self.assertEqual(report['records'][0]['status'],'passed')
                self.assertTrue(report['source_changed_during_run'])
                self.assert_rejected(status,report,'source_changed')

    def test_explicit_no_reference(self):
        status,report=self.run_case('explicit-no-reference',no_reference=True,
                                    reference_error=RuntimeError('must never be called'))
        self.assertEqual(status,0);self.assertFalse(report['gate']['reference_enabled'])
        self.assertNotIn('reference',report['records'][0])

    def test_report_only_mode_still_exposes_failure(self):
        status,report=self.run_case('report-only-reference-failure',reference=('42',),gate=False)
        self.assertEqual(status,0);self.assertFalse(report['gate']['passed'])

    def test_explicit_reference_representation(self):
        status,report=self.run_case('explicit-reference-byte-representation',reference=('42',),reference_expected=('42',))
        self.assertEqual(status,0);self.assertTrue(report['gate']['passed'])
        reference=report['records'][0]['reference']
        self.assertEqual(reference['expectation_field'],'expect.referenceDecode.binaryHex')
        self.assertEqual(reference['expectedBinaryHex'],['42'])
        self.assertEqual(report['records'][0]['details']['decoded']['binaryHex'],['41'])

    @contextlib.contextmanager
    def cpu_environment(self,*,system='linux',cpuinfo='',processor='Fallback CPU'):
        original_read=pathlib.Path.read_text

        def read_text(path,*args,**kwargs):
            if path==pathlib.Path('/proc/cpuinfo'):
                if not system.startswith('linux'):
                    raise AssertionError('Non-Linux metadata must not read /proc/cpuinfo')
                if isinstance(cpuinfo,BaseException):raise cpuinfo
                return cpuinfo
            return original_read(path,*args,**kwargs)

        with mock.patch.object(EVALUATOR.sys,'platform',system), \
             mock.patch.object(EVALUATOR.platform,'platform',return_value='controlled-'+system), \
             mock.patch.object(EVALUATOR.platform,'machine',return_value='controlled-machine'), \
             mock.patch.object(EVALUATOR.platform,'processor',side_effect=processor if isinstance(processor,BaseException) else None,
                               return_value=processor if isinstance(processor,str) else ''), \
             mock.patch.object(pathlib.Path,'read_text',read_text):
            yield

    def test_optional_cpu_metadata_report_completion(self):
        cases=[('absent-proc',{'cpuinfo':FileNotFoundError('controlled absent proc')},'Fallback CPU'),
               ('denied-proc',{'cpuinfo':PermissionError('controlled denied proc')},'Fallback CPU'),
               ('empty-proc',{'cpuinfo':''},'Fallback CPU'),
               ('malformed-proc',{'cpuinfo':'model name without colon\nmodel name :  \nmodel name extra: wrong\n'},'Fallback CPU'),
               ('valid-linux-proc',{'cpuinfo':'processor : 0\nmodel name\t: Known Linux CPU \n','processor':RuntimeError('fallback must not run')},'Known Linux CPU'),
               ('macos-native-fallback',{'system':'darwin','processor':' Apple M3 '},'Apple M3'),
               ('windows-native-fallback',{'system':'win32','processor':' Intel64 Family 6 '},'Intel64 Family 6'),
               ('empty-native-fallback',{'system':'darwin','processor':'  '},None),
               ('denied-native-fallback',{'system':'win32','processor':PermissionError('controlled fallback denied')},None)]
        for name,options,expected_cpu in cases:
            with self.subTest(name=name),self.cpu_environment(**options):
                status,report=self.run_case('cpu-'+name)
                self.assertEqual(status,0)
                self.assertTrue(report['gate']['passed'])
                self.assertEqual(report['environment']['cpu'],expected_cpu)

    def test_optional_cpu_metadata_keeps_failed_gate(self):
        with self.cpu_environment(cpuinfo=FileNotFoundError('controlled absent proc')):
            status,report=self.run_case('cpu-absent-reference-still-fails',reference=('42',))
            self.assertEqual(report['environment']['cpu'],'Fallback CPU')
            self.assert_rejected(status,report,'reference')

    def test_unrelated_metadata_exception_not_hidden(self):
        with self.cpu_environment(system='darwin',processor=RuntimeError('controlled unrelated bug')):
            with self.assertRaisesRegex(RuntimeError,'controlled unrelated bug'):
                self.run_case('cpu-unrelated-runtimeerror')
        METADATA_EXCEPTION_CONTROLS.append({'name':'unrelated-processor-runtimeerror','observed':'RuntimeError propagated','passed':True})

    def test_unrelated_report_write_error_not_hidden(self):
        original_write=pathlib.Path.write_text

        def write_text(path,*args,**kwargs):
            if path.name=='control.json':raise PermissionError('controlled report write denied')
            return original_write(path,*args,**kwargs)

        with self.cpu_environment(cpuinfo=FileNotFoundError('controlled absent proc')), \
             mock.patch.object(pathlib.Path,'write_text',write_text):
            with self.assertRaisesRegex(PermissionError,'controlled report write denied'):
                self.run_case('cpu-unrelated-report-write-error')
        METADATA_EXCEPTION_CONTROLS.append({'name':'unrelated-report-write-error','observed':'PermissionError propagated','passed':True})


if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(EvaluatorGateTests)
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    evidence={'schema':'specqr-evaluator-gate-controls-v1','evaluator':str(ARGS.evaluator),
              'evaluator_sha256':hashlib.sha256(ARGS.evaluator.read_bytes()).hexdigest(),
              'test_source_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
              'tests_run':result.testsRun,'assertion_failures':len(result.failures),'errors':len(result.errors),
              'controls':CONTROLS,'passed':result.wasSuccessful(),
              'metadata_exception_controls':METADATA_EXCEPTION_CONTROLS,
              'scope':'Main-entrypoint regression-gate integration controls; CLI and reference adapters deliberately stubbed, not QR algorithm coverage.'}
    ARGS.output.parent.mkdir(parents=True,exist_ok=True)
    ARGS.output.write_text(json.dumps(evidence,indent=2)+'\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
