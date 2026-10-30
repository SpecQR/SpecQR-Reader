// Direct ABI adapter shared by the Node parity and browser exception checks.
export function invoke(module, spec, data) {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  const ptr = module._specqr_alloc(bytes.byteLength);
  if (!ptr) throw new Error(`specqr_alloc(${bytes.byteLength}) failed`);
  let result;
  try {
    module.HEAPU8.set(bytes, ptr);
    const output = module._specqr_decode(spec.kind === 'grid' ? 0 : 1, ptr,
      spec.length ?? bytes.byteLength, spec.width, spec.height, spec.stride,
      spec.channels, spec.flags || 0, spec.maxCandidates || 0,
      spec.maxSymbols || 0, spec.maxGridAttempts || 0);
    if (!output) throw new Error('specqr_decode returned null');
    const length = module._specqr_result_size();
    result = JSON.parse(new TextDecoder().decode(module.HEAPU8.subarray(output, output + length)));
  } finally {
    module._specqr_release_result();
    module._specqr_free(ptr);
  }
  return {result, liveInputBytes: module._specqr_live_input_bytes(), resultBytes: module._specqr_result_size(), heapBytes: module.HEAPU8.buffer.byteLength};
}

// Only runtime/process measurements are outside the equivalence contract.
export function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().filter(k => !['runtime_ms', 'repeat', 'peak_rss_kib'].includes(k)).map(k => [k, canonical(value[k])]));
  return value;
}
export const equal = (a, b) => JSON.stringify(canonical(a)) === JSON.stringify(canonical(b));
