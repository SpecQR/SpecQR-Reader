export {renderResult,resultLabel,escapedAscii} from './renderer.mjs';
import {renderResult,escapedAscii} from './renderer.mjs';

// Acceptance harness only. The normal app imports renderer.mjs directly.
export function testUntrustedRendering() {
  const payload='<img src=x onerror="window.__specqrPayloadExecuted=true"><script>window.__specqrPayloadExecuted=true</script>https://example.invalid/';
  const hex=Array.from(payload,c=>c.charCodeAt(0).toString(16).padStart(2,'0')).join('');
  const container=document.createElement('div');
  const result={ok:true,error:'none',message:payload,work_limit_reached:false,symbols:[{
    bytes_hex:hex,version:1,ec_level:'L',segments:[{mode:4,bytes_hex:hex,eci:-1}],fnc1:0,
    fnc1_application_indicator:-1,structured_append:{present:false,index:0,count:0,parity:0},trace:[{stage:'test',detail:payload}]
  }],trace:[]};
  renderResult(container,result);
  const forbiddenElements=container.querySelectorAll('img,script,a,iframe,svg,object').length;
  const preservesLiteralText=container.textContent.includes(payload);
  const preservesBytes=container.textContent.includes(hex);
  const preservesAscii=escapedAscii(hex)===payload;
  return {pass:forbiddenElements===0&&preservesLiteralText&&preservesBytes&&preservesAscii,
    forbiddenElements,preservesLiteralText,preservesBytes,preservesAscii};
}
