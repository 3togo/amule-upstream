// Run: node --experimental-vm-modules unittests/tests/ClientSoftwareTest.mjs
// Evaluate the real table module with UI/network imports stubbed: these tests
// exercise its exported formatter and both column sets without a running daemon.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../../src/webapi/static/js/views/client-table.js', import.meta.url), 'utf8');
const english = JSON.parse(fs.readFileSync(new URL('../../src/webapi/static/i18n/en.json', import.meta.url), 'utf8'));
const module = new vm.SourceTextModule(source);
await module.link((specifier) => {
  const imports = source.match(new RegExp(`import \\{([^}]+)\\} from "${specifier.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}";`))[1]
    .split(',').map(name => name.trim());
  return new vm.SyntheticModule(imports, function () {
    for (const name of imports) this.setExport(name, name === 't' ? key => english[key] || key : () => {});
  });
});
await module.evaluate();
const { softLabel, COLS, KNOWN_COLS } = module.namespace;
const cases = [
  [{ software: 'emule', software_version: 'v0.70b', client_mod_name: 'AI v1.6' }, 'eMule (AI v1.6) v0.70b'],
  [{ software: 'emule', software_version: 'v0.70b', client_mod_name: 'eMule Qt 1.0' }, 'eMule (eMule Qt 1.0) v0.70b'],
  [{ software: 'emule', software_version: 'v0.70b', client_mod_name: null }, 'eMule v0.70b'],
  [{ software: 'amule', software_version: 'v2.3.3', client_mod_name: 'aMule' }, 'aMule v2.3.3'],
  [{ software: 'amule', software_version: 'v2.3.3', client_mod_name: 'aMule SVN' }, 'aMule SVN v2.3.3'],
  [{ software: 'old_emule', software_version: 'v0.49a', client_mod_name: 'old-mod' }, 'Old eMule (old-mod) v0.49a'],
  [{}, '—'],
];
for (const [row, expected] of cases) {
  assert.equal(softLabel(row), expected);
  for (const columns of [COLS, KNOWN_COLS]) {
    assert.equal(columns.find(column => column.key === 'software').cell(row), expected);
  }
}
console.log('PASS: shared live/history/detail software label, stock clients and missing identity');
