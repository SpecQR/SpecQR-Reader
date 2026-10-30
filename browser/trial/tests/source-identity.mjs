import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
export const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
export function sourceIdentity(root) {
  const paths = [];
  function visit(relative) {
    for (const name of fs.readdirSync(path.join(root, relative)).sort()) {
      const child = path.posix.join(relative, name);
      if (fs.statSync(path.join(root, child)).isDirectory()) visit(child);
      else paths.push(child);
    }
  }
  visit('src'); visit('include');
  paths.push('browser/bridge.cpp', 'browser/bridge.h', 'browser/native_driver.cpp',
    'browser/contract_tests.cpp', 'browser/build.sh', 'browser/emscripten-version.txt', 'browser/toolchain.json');
  const files = paths.sort().map(file => ({path: file, sha256: sha256(fs.readFileSync(path.join(root, file)))}));
  return {algorithm: 'SHA256 of sorted path + NUL + file SHA256 + LF',
    aggregateSha256: sha256(files.map(f => f.path + '\0' + f.sha256 + '\n').join('')), files};
}
