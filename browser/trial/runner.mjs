export const MAX_DIMENSION = 8192;
export const MAX_PIXELS = 16 * 1024 * 1024;
export const MAX_BYTES = 64 * 1024 * 1024;

const uint = (value, name) => {
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff) {
    throw new RangeError(`${name}: 0～4294967295 の整数が必要です。`);
  }
  return value;
};

export function validateDimensions(width, height) {
  uint(width, 'width'); uint(height, 'height');
  if (!width || !height || width > MAX_DIMENSION || height > MAX_DIMENSION || width * height > MAX_PIXELS) {
    throw new RangeError('画像は各辺 8192 px 以下、合計 16,777,216 px 以下である必要があります。自動縮小はしません。');
  }
}

export function validatePayload(input) {
  if (!input || !['grid', 'image'].includes(input.kind)) throw new TypeError('kind は grid または image が必要です。');
  let data;
  if (input.data instanceof Uint8Array) data = input.data;
  else if (input.data instanceof ArrayBuffer) data = new Uint8Array(input.data);
  else throw new TypeError('data は Uint8Array または ArrayBuffer が必要です。');
  if (!data.byteLength || data.byteLength > MAX_BYTES) throw new RangeError('入力バイト数が範囲外です。');
  const width = uint(input.width, 'width');
  const height = uint(input.height ?? (input.kind === 'grid' ? width : 0), 'height');
  const channels = uint(input.channels ?? 1, 'channels');
  const stride = uint(input.stride ?? width * channels, 'stride');
  if (input.kind === 'grid') {
    if (width < 21 || width > 177 || (width - 21) % 4 || height !== width || channels !== 1) throw new RangeError('QR Model 2 のグリッド寸法が不正です。');
  } else {
    validateDimensions(width, height);
    if (![1, 3].includes(channels)) throw new RangeError('channels は 1（灰色）または 3（RGB）が必要です。');
  }
  if (stride < width * channels || (height - 1) * stride + width * channels > data.byteLength) throw new RangeError('stride または入力バイト数が寸法と一致しません。');
  const options = input.options ?? {};
  if (typeof options !== 'object' || Array.isArray(options)) throw new TypeError('options が不正です。');
  for (const key of Object.keys(options)) if (!['diagnostics','try_invert','strict_padding','try_mirror','limits'].includes(key)) throw new TypeError(`未対応の option: ${key}`);
  for (const key of ['diagnostics','try_invert','strict_padding','try_mirror']) {
    if (key in options && typeof options[key] !== 'boolean') throw new TypeError(`${key} は boolean が必要です。`);
  }
  const limits = options.limits ?? {};
  if (typeof limits !== 'object' || Array.isArray(limits)) throw new TypeError('limits が不正です。');
  for (const key of Object.keys(limits)) if (!['max_candidates','max_symbols','max_grid_attempts'].includes(key)) throw new TypeError(`未対応の limit: ${key}`);
  for (const [key,value] of Object.entries(limits)) uint(value,key);
  return {kind:input.kind,data,width,height,stride,channels,options:{...options,limits:{...limits}}};
}

export class ReaderWorkerClient {
  constructor({workerURL = new URL('./worker.js', import.meta.url), onState = () => {}, startupTimeout = 45000} = {}) {
    this.workerURL = new URL(workerURL, import.meta.url);
    if (this.workerURL.origin !== location.origin) throw new Error('Worker は同一オリジンである必要があります。');
    this.onState = onState;
    this.startupTimeout = startupTimeout;
    this.worker = null;
    this.pending = null;
    this.generation = 0;
    this.sequence = 0;
    this.ready = false;
    this.timer = null;
  }

  run(input) {
    if (this.pending) return Promise.reject(new Error('前の処理が終了するか、停止してから実行してください。'));
    let payload;
    try {
      const checked = validatePayload(input);
      payload = {...checked,data:new Uint8Array(checked.data)};
    } catch (error) { return Promise.reject(error); }
    return new Promise((resolve,reject) => {
      this.pending = {id:++this.sequence,payload,resolve,reject};
      try {
        if (!this.worker) this.createWorker();
        if (this.ready) this.sendPending();
      } catch (error) { this.fail(error); }
    });
  }

  createWorker() {
    const generation = ++this.generation;
    const worker = new Worker(this.workerURL,{type:'module',name:'SpecQR Reader'});
    this.worker = worker;
    this.ready = false;
    this.onState({state:'loading',message:'Reader / WebAssembly を準備しています…'});
    this.timer = setTimeout(() => {
      if (generation === this.generation) this.fail(new Error('Reader の準備が時間内に完了しませんでした。再実行できます。'));
    },this.startupTimeout);
    worker.onmessage = ({data}) => {
      if (generation !== this.generation || worker !== this.worker) return;
      if (!data || typeof data !== 'object') return this.fail(new Error('Worker の応答が不正です。'));
      if (data.type === 'ready') {
        clearTimeout(this.timer); this.timer = null; this.ready = true;
        this.sendPending();
      } else if (data.type === 'fatal') {
        this.fail(new Error(data.message || 'Worker の初期化に失敗しました。'));
      } else if (this.pending && data.id === this.pending.id) {
        if (data.type === 'result') {
          if (!data.result || !Array.isArray(data.result.symbols) || typeof data.result.error !== 'string' || typeof data.result.ok !== 'boolean') {
            this.fail(new Error('Reader の応答形式が不正です。')); return;
          }
          const pending = this.pending; this.pending = null;
          this.onState({state:'idle',message:'処理が完了しました。'});
          pending.resolve({result:data.result,diagnostics:data.diagnostics});
        } else if (data.type === 'error') this.fail(new Error(data.message || 'Worker の処理に失敗しました。'));
        else this.fail(new Error('Worker の応答種別が不正です。'));
      }
    };
    worker.onerror = event => {
      if (generation !== this.generation) return;
      event.preventDefault();
      this.fail(new Error(`Worker エラー: ${event.message || '読み込みまたは実行に失敗しました。再実行できます。'}`));
    };
    worker.onmessageerror = () => {
      if (generation === this.generation) this.fail(new Error('Worker の応答を受信できませんでした。再実行できます。'));
    };
  }

  sendPending() {
    if (!this.pending || !this.ready || this.pending.sent) return;
    this.pending.sent = true;
    const {id,payload} = this.pending;
    this.onState({state:'running',message:'C++ Reader で解析しています…'});
    try { this.worker.postMessage({type:'decode',id,payload},[payload.data.buffer]); }
    catch (error) { this.fail(error); }
  }

  destroyWorker() {
    ++this.generation;
    clearTimeout(this.timer); this.timer = null;
    if (this.worker) { this.worker.terminate(); this.worker = null; }
    this.ready = false;
  }

  fail(error) {
    const pending = this.pending; this.pending = null;
    this.destroyWorker();
    this.onState({state:'error',message:error.message});
    if (pending) pending.reject(error);
  }

  stop() {
    const pending = this.pending; this.pending = null;
    this.destroyWorker();
    this.onState({state:'stopped',message:'停止しました。再実行すると新しい Worker で開始します。'});
    if (pending) pending.reject(new DOMException('利用者が処理を停止しました。','AbortError'));
  }

  dispose() { this.stop(); }
}
