import {validateDimensions,MAX_BYTES} from './runner.mjs';
export {validateDimensions};

export function rgbaToRgb(rgba,width,height) {
  validateDimensions(width,height);
  if (!(rgba instanceof Uint8ClampedArray || rgba instanceof Uint8Array) || rgba.length !== width*height*4) throw new RangeError('RGBA の長さが寸法と一致しません。');
  const data = new Uint8Array(width*height*3);
  let transparentPixels = 0;
  for (let i=0,j=0;i<rgba.length;i+=4,j+=3) {
    const alpha = rgba[i+3];
    if (alpha !== 255) transparentPixels++;
    for (let c=0;c<3;c++) data[j+c] = Math.round((rgba[i+c]*alpha+255*(255-alpha))/255);
  }
  return {data,transparentPixels};
}

function inspectImage(bytes) {
  const view = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if (bytes.length >= 24 && [137,80,78,71,13,10,26,10].every((n,i)=>bytes[i]===n)) {
    if (view.getUint32(8) !== 13 || String.fromCharCode(...bytes.subarray(12,16)) !== 'IHDR') throw new Error('PNG の IHDR が不正です。');
    const width=view.getUint32(16),height=view.getUint32(20);
    validateDimensions(width,height);
    return {mime:'image/png',format:'PNG',width,height};
  }
  if (bytes.length >= 4 && bytes[0]===255 && bytes[1]===216) {
    let offset=2;
    while (offset<bytes.length) {
      if (bytes[offset++] !== 255) throw new Error('JPEG のマーカーが不正です。');
      while (bytes[offset]===255) offset++;
      const marker=bytes[offset++];
      if (marker===217 || marker===218) break;
      if (marker===1 || (marker>=208 && marker<=215)) continue;
      if (offset+2>bytes.length) break;
      const length=view.getUint16(offset);
      if (length<2 || offset+length>bytes.length) break;
      if ([192,193,194,195,197,198,199,201,202,203,205,206,207].includes(marker)) {
        if (length<8) break;
        const height=view.getUint16(offset+3),width=view.getUint16(offset+5);
        validateDimensions(width,height);
        return {mime:'image/jpeg',format:'JPEG',width,height};
      }
      offset+=length;
    }
    throw new Error('JPEG の画像寸法を読み取れませんでした。');
  }
  throw new Error('PNG または JPEG の画像ファイルを選んでください。');
}

export async function decodeBrowserImage(file) {
  if (!(file instanceof Blob) || !file.size || file.size>MAX_BYTES) throw new RangeError('画像ファイルは 0 B より大きく 64 MiB 以下である必要があります。');
  const bytes=new Uint8Array(await file.arrayBuffer());
  const header=inspectImage(bytes);
  const safeBlob=new Blob([bytes],{type:header.mime});
  const url=URL.createObjectURL(safeBlob);
  const image=new Image();
  try {
    await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=()=>reject(new Error('ブラウザで画像を展開できませんでした。'));image.src=url;});
    const width=image.naturalWidth,height=image.naturalHeight;
    validateDimensions(width,height);
    // Decode at natural dimensions; no resampling or character decoding takes place.
    const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
    try {
      const context=canvas.getContext('2d',{willReadFrequently:true});
      if (!context) throw new Error('Canvas 2D を利用できません。');
      context.drawImage(image,0,0);
      const {data,transparentPixels}=rgbaToRgb(context.getImageData(0,0,width,height).data,width,height);
      return {kind:'image',data,width,height,stride:width*3,channels:3,format:header.format,
        transparentPixels,codec:'browser-canvas',headerWidth:header.width,headerHeight:header.height};
    } finally { canvas.width=1;canvas.height=1; }
  } finally { image.src='';URL.revokeObjectURL(url); }
}

export function decodePnm(buffer) {
  const bytes=buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  if (!bytes.length || bytes.length>MAX_BYTES) throw new RangeError('PNM のサイズが範囲外です。');
  let pos=0;
  const space=n=>[9,10,13,32].includes(n);
  const token=()=>{
    while (pos<bytes.length) {
      if (space(bytes[pos])) {pos++;continue;}
      if (bytes[pos]===35) {while(pos<bytes.length && bytes[pos]!==10)pos++;continue;}
      break;
    }
    let value='';
    while(pos<bytes.length && !space(bytes[pos]) && bytes[pos]!==35) {
      if(value.length>=32)throw new Error('PNM のヘッダーが長すぎます。');
      value+=String.fromCharCode(bytes[pos++]);
    }
    return value;
  };
  const magic=token();
  if(!['P5','P6'].includes(magic))throw new Error('サンプルは P5/P6 PNM のみ対応します。');
  const number=()=>{const value=token();if(!/^\d+$/.test(value))throw new Error('PNM の数値が不正です。');return Number(value);};
  const width=number(),height=number(),maxval=number();
  validateDimensions(width,height);
  if(maxval!==255 || !space(bytes[pos]))throw new Error('PNM の maxval または区切りが不正です。');
  if(bytes[pos++]===13 && bytes[pos++]!==10)throw new Error('PNM の CR 区切りは CRLF が必要です。');
  const channels=magic==='P6'?3:1;
  if(bytes.length-pos!==width*height*channels)throw new Error('PNM のデータ長が一致しません。');
  return {kind:'image',data:bytes.slice(pos),width,height,stride:width*channels,channels,format:magic,codec:'raw-pnm',transparentPixels:0};
}

export function decodeGrid(text) {
  if(typeof text!=='string' || text.length>64*1024)throw new Error('グリッドのサイズが範囲外です。');
  const lines=text.split(/\r?\n/);if(lines.at(-1)==='')lines.pop();
  if(!/^\d+$/.test(lines[0]))throw new Error('グリッドの寸法が不正です。');
  const width=Number(lines.shift());
  if(width<21 || width>177 || (width-21)%4 || lines.length!==width || lines.some(line=>line.length!==width || /[^01]/.test(line)))throw new Error('グリッドの行数またはモジュールが不正です。');
  return {kind:'grid',data:Uint8Array.from(lines.join(''),c=>Number(c)),width,height:width,stride:width,channels:1,format:'module grid',codec:'raw-grid',transparentPixels:0};
}
