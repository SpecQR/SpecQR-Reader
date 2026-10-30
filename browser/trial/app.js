import {ReaderWorkerClient} from './runner.mjs';
import {decodeBrowserImage,decodePnm,decodeGrid} from './codec.mjs';
import {renderResult,resultLabel} from './renderer.mjs';

const $=id=>document.getElementById(id);
const canvas=$('preview');
const context=canvas.getContext('2d');
const emptyResults=$('result').firstElementChild.cloneNode(true);
const manifestURL=new URL('./samples/manifest.json',import.meta.url);
const sampleLabels={
  'binary-nul-invalid-utf8':'NUL / 非 UTF-8 のバイト列',
  'fnc1-first-alpha-gs':'FNC1・第1位置',
  'fnc1-second-mixed':'FNC1・第2位置と混合モード',
  'metadata-eci-utf8':'ECI 26 のメタデータ',
  'metadata-sa-0':'Structured Append・1 / 2',
  'metadata-sa-1':'Structured Append・2 / 2',
  'single-binary-gray':'1 個の QR・グレースケール',
  'single-binary-rgb':'1 個の QR・RGB',
  'different-payloads-gray':'異なる内容の QR が 2 個',
  'same-payload-two-gray':'同じ内容の QR が 2 個',
  'structured-append-pair-gray':'Structured Append が 2 個',
  'blank-white-gray':'何もない白い画像',
};
let samples=[];
let selected=null;
let currentResult=null;
let version=0;
let running=false;
let loading=false;
let sampleAbort=null;

function status(text,tone='') {$('status').textContent=text;$('status').className=`status ${tone}`;}
const client=new ReaderWorkerClient({onState:event=>{if(running && ['loading','running'].includes(event.state))status(event.message,'running');}});
function controls() {
  $('run').disabled=!selected || running || loading;
  $('stop').disabled=!running && !loading;
  $('clear').disabled=!selected && !running && !loading && !currentResult;
  $('save-json').disabled=!currentResult;
  for(const id of ['diagnostics','invert','strict-padding','max-candidates','max-symbols','max-attempts'])$(id).disabled=running;
}
function clearResult() {
  currentResult=null;$('result').replaceChildren(emptyResults.cloneNode(true));$('run-stats').textContent='';
}
function resetInput() {
  ++version;client.stop();running=false;loading=false;
  sampleAbort?.abort();sampleAbort=null;
  selected=null;clearResult();
  canvas.width=1;canvas.height=1;canvas.hidden=true;$('preview-empty').hidden=false;
  $('input-name').textContent='入力を待っています';$('input-size').textContent='';$('input-badge').textContent='未選択';
  $('codec-note').textContent='プレビューの表示倍率だけを調整します。解析用の画素を自動縮小しません。';
  controls();
}
function drawPreview(symbols=[]) {
  if(!selected || !context)return;
  const {width,height,data,channels,stride,kind}=selected;
  const scale=kind==='grid'?8:1,pad=kind==='grid'?4:0;
  canvas.width=(width+pad*2)*scale;canvas.height=(height+pad*2)*scale;
  context.fillStyle='#fff';context.fillRect(0,0,canvas.width,canvas.height);
  if(kind==='grid') {
    context.fillStyle='#142b26';
    for(let y=0;y<height;y++)for(let x=0;x<width;x++)if(data[y*stride+x])context.fillRect((x+pad)*scale,(y+pad)*scale,scale,scale);
  } else {
    const image=context.createImageData(width,height);
    for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
      const i=y*stride+x*channels,o=(y*width+x)*4;
      image.data[o]=data[i];image.data[o+1]=data[i+(channels===3?1:0)];image.data[o+2]=data[i+(channels===3?2:0)];image.data[o+3]=255;
    }
    context.putImageData(image,0,0);
  }
  symbols.forEach((symbol,index)=>{
    if(!Array.isArray(symbol.corners) || symbol.corners.length!==4 || symbol.corners.some(point=>!Array.isArray(point)||point.length!==2||point.some(n=>!Number.isFinite(n))))return;
    const points=symbol.corners.map(([x,y])=>[(x+pad)*scale,(y+pad)*scale]);
    if(points.every(([x,y])=>x===pad*scale&&y===pad*scale))return;
    context.beginPath();points.forEach(([x,y],i)=>i?context.lineTo(x,y):context.moveTo(x,y));context.closePath();
    context.lineWidth=Math.max(2,canvas.width/220);context.strokeStyle='#00a46b';context.fillStyle='#00a46b12';context.fill();context.stroke();
    const fontSize=Math.max(12,canvas.width/35);context.font=`600 ${fontSize}px sans-serif`;
    const [x,y]=points[0];const label=`${index+1}`;const textWidth=context.measureText(label).width;
    context.fillStyle='#087a54';context.fillRect(x,Math.max(0,y-fontSize*1.6),textWidth+fontSize*.7,fontSize*1.5);
    context.fillStyle='#fff';context.fillText(label,x+fontSize*.35,Math.max(fontSize*1.1,y-fontSize*.45));
  });
  canvas.hidden=false;$('preview-empty').hidden=true;
}
function selectInput(input,name) {
  selected={...input,name};
  $('input-name').textContent=name;
  $('input-size').textContent=`${input.width} × ${input.height} ${input.kind==='grid'?'modules':'px'}`;
  $('input-badge').textContent=input.format;
  if(input.codec==='browser-canvas')$('codec-note').textContent=`ブラウザで ${input.format} → RGBA → RGB。透過 ${input.transparentPixels.toLocaleString()} 画素を白に合成。解析画像は原寸${input.headerWidth!==input.width||input.headerHeight!==input.height?'（向きの反映後）':''}、自動縮小なし。`;
  else if(input.kind==='grid')$('codec-note').textContent='元の 0/1 モジュールを decode_grid に渡します。表示のみ 8 倍・余白付きです。';
  else $('codec-note').textContent=`PNM の生画素（${input.channels===3?'RGB':'8-bit gray'}）を read_image に渡します。画像コーデック・縮小は介しません。`;
  drawPreview();status('準備できました。「読み取り」を押してください。');controls();
}
async function loadInput(loader,name) {
  resetInput();loading=true;const ticket=version;controls();status('画像を準備しています…','running');
  try {
    const input=await loader();
    if(ticket!==version)return;
    loading=false;selectInput(input,name);
  } catch(error) {
    if(ticket!==version)return;
    loading=false;status(error.message,'error');controls();
  }
}
$('image-file').addEventListener('change',()=>{
  const file=$('image-file').files[0];if(!file)return;
  $('sample').value='';
  loadInput(()=>decodeBrowserImage(file),file.name);
  // Allow selection of the same file again after an error or cancellation.
  $('image-file').value='';
});
$('sample').addEventListener('change',()=>{
  const sample=samples.find(item=>item.id===$('sample').value);
  if(!sample){resetInput();status('画像またはサンプルを選んでください。');return;}
  loadInput(async()=>{
    sampleAbort=new AbortController();
    const url=new URL(sample.path,manifestURL);
    if(url.origin!==location.origin)throw new Error('サンプルは同一オリジンからのみ読み込めます。');
    const response=await fetch(url,{signal:sampleAbort.signal});
    if(!response.ok)throw new Error(`サンプルを読み込めませんでした（HTTP ${response.status}）。`);
    if(sample.kind==='grid')return decodeGrid(await response.text());
    if(sample.format==='pnm')return decodePnm(await response.arrayBuffer());
    return decodeBrowserImage(await response.blob());
  },sampleLabels[sample.id]??sample.label);
});
function options() {
  const limits={};
  for(const [id,key,max] of [['max-candidates','max_candidates',96],['max-symbols','max_symbols',32],['max-attempts','max_grid_attempts',2048]]){
    const field=$(id),value=Number(field.value);
    if(!Number.isInteger(value)||value<0||value>max||field.value==='')throw new Error(`${field.parentElement.textContent.trim()}: 0～${max} の整数を指定してください。`);
    limits[key]=value;
  }
  return {diagnostics:$('diagnostics').checked,try_invert:$('invert').checked,strict_padding:$('strict-padding').checked,limits};
}
$('run').addEventListener('click',async()=>{
  if(!selected||running||loading)return;
  const ticket=version;
  try {
    const settings=options();clearResult();drawPreview();running=true;controls();
    const {result,diagnostics}=await client.run({...selected,options:settings});
    if(ticket!==version)return;
    currentResult=result;renderResult($('result'),result);drawPreview(result.symbols);
    const label=resultLabel(result);status(label.text,label.tone==='error'?'error':'');
    $('run-stats').textContent=`Worker 内処理 ${diagnostics.elapsed_ms.toFixed(1)} ms · 入力メモリ解放後 ${diagnostics.live_input_bytes} B · Wasm ヒープ ${(diagnostics.heap_bytes/1048576).toFixed(1)} MiB`;
  } catch(error) {
    if(ticket!==version)return;
    if(error.name==='AbortError')status('停止しました。再実行すると新しい Worker で開始します。');
    else status(`${error.message} 必要に応じて再実行してください。`,'error');
  } finally {if(ticket===version){running=false;controls();}}
});
$('stop').addEventListener('click',()=>{
  ++version;sampleAbort?.abort();sampleAbort=null;client.stop();loading=false;running=false;
  clearResult();if(selected)drawPreview();controls();status('停止しました。再実行すると新しい Worker で開始します。');
});
$('clear').addEventListener('click',()=>{resetInput();$('sample').value='';status('画像またはサンプルを選んでください。');});
$('save-json').addEventListener('click',()=>{
  if(!currentResult)return;
  const url=URL.createObjectURL(new Blob([JSON.stringify(currentResult,null,2)+'\n'],{type:'application/json'}));
  const link=document.createElement('a');link.href=url;link.download='specqr-result.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
});
window.addEventListener('pagehide',()=>client.dispose());
if(location.protocol==='file:')status('launch.command または案内のローカル HTTP サーバーから開いてください。file:// では Worker を読み込めません。','error');
else fetch(manifestURL).then(response=>{if(!response.ok)throw new Error(`HTTP ${response.status}`);return response.json();}).then(manifest=>{
  samples=Array.isArray(manifest.samples)?manifest.samples:[];
  for(const [title,filter] of [
    ['ブラウザの画像入力',sample=>sample.format==='png'||sample.format==='jpeg'],
    ['Reader の画像サンプル',sample=>sample.format==='pnm'],
    ['モジュールグリッド',sample=>sample.kind==='grid'],
  ]){
    const group=document.createElement('optgroup');group.label=title;
    for(const sample of samples.filter(filter)){const option=document.createElement('option');option.value=sample.id;option.textContent=sampleLabels[sample.id]??sample.label;group.append(option);}
    if(group.children.length)$('sample').append(group);
  }
}).catch(error=>{status(`サンプル一覧を読み込めませんでした（${error.message}）。画像ファイルは選択できます。`,'error');});
