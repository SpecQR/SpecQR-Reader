// Fixed acceptance inventory: a missing input must fail, not reduce the gate.
export const FIXTURES = Object.freeze({
  'binary-nul-invalid-utf8': ['grid', 1],
  'fnc1-first-alpha-gs': ['grid', 1],
  'fnc1-second-mixed': ['grid', 1],
  'metadata-eci-utf8': ['grid', 1],
  'metadata-sa-0': ['grid', 1],
  'metadata-sa-1': ['grid', 1],
  'single-binary-gray': ['image', 1],
  'single-binary-rgb': ['image', 1],
  'different-payloads-gray': ['image', 2],
  'same-payload-two-gray': ['image', 2],
  'structured-append-pair-gray': ['image', 2],
  'blank-white-gray': ['image', 0]
});
export const CODECS = Object.freeze(['codec-png', 'codec-jpeg', 'codec-transparent-png']);
function require(condition, message) { if (!condition) throw new Error('Acceptance coverage: ' + message); }
function exact(actual, expected, label) {
  require(Array.isArray(actual) && actual.length === expected.length &&
    new Set(actual).size === expected.length && expected.every(id => actual.includes(id)), label);
}
export function validateManifest(manifest) {
  require(Array.isArray(manifest?.samples), 'samples array is missing');
  const fixtures = manifest.samples.filter(s => s.acceptance);
  exact(fixtures.map(f => f.id), Object.keys(FIXTURES), 'expected exactly 12 distinct fixture IDs');
  for (const f of fixtures) require(f.kind === FIXTURES[f.id][0] && f.expectedSymbols?.length === FIXTURES[f.id][1], f.id + ' kind or expected symbol count');
  exact(manifest.samples.filter(s => s.codecTest).map(s => s.id), CODECS, 'expected exactly 3 distinct codec IDs');
}
export function validateFixtureResults(rows) {
  require(Array.isArray(rows), 'fixture results array is missing');
  const expected = Object.keys(FIXTURES).flatMap(id => [id + ':false', id + ':true']);
  exact(rows.map(r => r.id + ':' + r.diagnostics), expected, 'expected 24 distinct normal/diagnostic runs');
  for (const r of rows) {
    require(typeof r.diagnostics === 'boolean', r.id + ' diagnostics is not boolean');
    const count = r.result ? r.result.symbols?.length : r.symbols;
    require(count === FIXTURES[r.id][1], r.id + ' decoded symbol count');
  }
  for (const diagnostics of [false, true]) require(rows.filter(r => r.diagnostics === diagnostics).reduce((n, r) => n + (r.result ? r.result.symbols.length : r.symbols), 0) === 14, 'expected 14 symbols in each lane');
}
export function validateCodecResults(rows) {
  require(Array.isArray(rows), 'codec results array is missing');
  exact(rows.map(r => r.id), CODECS, 'expected exactly 3 distinct codec results');
}
export function validateBrowserReport(report) {
  require(report?.pass === true, 'source browser report did not pass');
  require(Array.isArray(report.assertions) && report.assertions.length > 0 && report.assertions.every(a => a.pass === true), 'source browser assertions did not all pass');
  validateFixtureResults(report.fixtureResults);
  require(report.fixtureResults.every(r => r.parity === true), 'source browser fixture parity failed');
  validateCodecResults(report.codecResults);
}
