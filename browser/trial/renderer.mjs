export function escapedAscii(hex) {
  if (typeof hex !== 'string' || hex.length % 2 || !/^[\da-f]*$/i.test(hex)) return '（不正な bytes_hex）';
  let text='';
  for (let i=0;i<hex.length;i+=2) {
    const byte=parseInt(hex.slice(i,i+2),16);
    text+=byte>=32 && byte<=126 && byte!==92 ? String.fromCharCode(byte) : `\\x${hex.slice(i,i+2).toLowerCase()}`;
  }
  return text || '（空のバイト列）';
}

function element(tag,text,className) {
  const node=document.createElement(tag);
  if(text!==undefined)node.textContent=text;
  if(className)node.className=className;
  return node;
}

export function resultLabel(result) {
  const count=Array.isArray(result.symbols)?result.symbols.length:0;
  if(result.work_limit_reached && count)return {text:`一部の結果 · ${count} シンボル`,tone:'warning'};
  if(result.work_limit_reached)return {text:'処理上限に到達',tone:'warning'};
  if(result.error==='not_found')return {text:'QR は見つかりませんでした',tone:'neutral'};
  if(result.ok)return {text:`読み取り完了 · ${count} シンボル`,tone:'success'};
  return {text:`Reader エラー · ${result.error}`,tone:'error'};
}

// All payload-derived content is a text node. URLs are never activated.
export function renderResult(container,result) {
  container.replaceChildren();
  const label=resultLabel(result);
  const summary=element('div',undefined,`result-summary ${label.tone}`);
  summary.append(element('strong',label.text));
  if(result.message)summary.append(element('p',String(result.message)));
  if(result.work_limit_reached)summary.append(element('p','work_limit_reached: true — 探索は完了していません。見つかったシンボルのみを表示しています。'));
  container.append(summary);
  for(const [index,symbol] of (result.symbols??[]).entries()) {
    const article=element('article',undefined,'symbol');
    const header=element('div',undefined,'symbol-heading');
    header.append(element('h3',`シンボル ${index+1}`),element('span',`Version ${symbol.version} · EC ${symbol.ec_level}`,'symbol-tag'));
    article.append(header,element('h4','元のバイト列 · bytes_hex'));
    article.append(element('pre',String(symbol.bytes_hex??''),'hex-value'));
    const preview=element('details');
    preview.append(element('summary','ASCII エスケープ表示（文字コード変換なし）'),element('pre',escapedAscii(symbol.bytes_hex),'ascii-value'));
    article.append(preview);
    const metadata=element('details');
    metadata.append(element('summary','segments / ECI / FNC1 / Structured Append など'));
    // Preserve every field rather than normalizing away metadata or partial statuses.
    metadata.append(element('pre',JSON.stringify(symbol,null,2),'json-value'));
    article.append(metadata);
    container.append(article);
  }
  const raw=element('details',undefined,'raw-details');
  raw.append(element('summary','Reader の全結果 JSON'),element('pre',JSON.stringify(result,null,2),'json-value'));
  container.append(raw);
}
