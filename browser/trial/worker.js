import createSpecQRModule from './assets/specqr.mjs';
import {validatePayload} from './runner.mjs';

let module;
let busy = false;

function decode(input) {
  const payload = validatePayload(input);
  const {data,width,height,stride,channels,options} = payload;
  const limits = options.limits;
  const flags = (options.diagnostics ? 1 : 0) | (options.try_invert ? 2 : 0) |
    (options.strict_padding ? 4 : 0) | (options.try_mirror === false ? 8 : 0);
  let pointer = 0;
  let result;
  let resultBytes = 0;
  const begin = performance.now();
  try {
    pointer = module._specqr_alloc(data.byteLength);
    if (!pointer) throw new Error('Reader の入力メモリを確保できませんでした。');
    // alloc may grow memory, so obtain HEAPU8 only after allocation.
    module.HEAPU8.set(data,pointer);
    const output = module._specqr_decode(payload.kind === 'grid' ? 0 : 1,pointer,data.byteLength,
      width,height,stride,channels,flags,limits.max_candidates ?? 0,limits.max_symbols ?? 0,limits.max_grid_attempts ?? 0);
    resultBytes = module._specqr_result_size();
    if (!output || !resultBytes) throw new Error('Reader から結果を受け取れませんでした。');
    result = JSON.parse(module.UTF8ToString(output,resultBytes));
  } finally {
    if (pointer) module._specqr_free(pointer);
    module._specqr_release_result();
  }
  return {result,diagnostics:{elapsed_ms:performance.now()-begin,heap_bytes:module.HEAPU8.byteLength,
    live_input_bytes:module._specqr_live_input_bytes(),result_bytes:module._specqr_result_size(),output_json_bytes:resultBytes}};
}

self.onmessage = ({data}) => {
  if (!data || data.type !== 'decode' || !Number.isSafeInteger(data.id)) return;
  if (!module || busy) {
    self.postMessage({type:'error',id:data.id,message:busy ? 'Worker は処理中です。' : 'Reader の準備ができていません。'});
    return;
  }
  busy = true;
  try { self.postMessage({type:'result',id:data.id,...decode(data.payload)}); }
  catch (error) { self.postMessage({type:'error',id:data.id,message:error instanceof Error ? error.message : String(error)}); }
  finally { busy = false; }
};

try {
  module = await createSpecQRModule({locateFile:path => new URL(`./assets/${path}`,import.meta.url).href});
  self.postMessage({type:'ready'});
} catch (error) {
  self.postMessage({type:'fatal',message:error instanceof Error ? error.message : String(error)});
}
