import { describe, expect, test } from 'vitest';
import { parseDiagnostics } from '../src/services/diagnosticsProtocol';

const report = {
  kind: 'diagnostics',
  version: 1,
  ok: true,
  capture_available: true,
  build_id: 'abc123',
  hardware: 'test_board',
  uptime_ms: 100,
  internal_free: 12000,
  internal_min: 10000,
  internal_largest: 8000,
  psram_free: 100000,
  overwritten: 2,
  truncated: 1,
  dropped: 3,
  log: 'W (123) PUBMOTE-WIFI: disconnected\n',
};

describe('device diagnostics', () => {
  test('reads a complete capture despite console echo and unrelated logs', () => {
    expect(
      parseDiagnostics([
        'diagnostics',
        'I (0) other: noise',
        JSON.stringify(report),
        'pubconsole>',
      ]),
    ).toEqual(report);
  });
  test('allows memory metrics when capture is unavailable', () => {
    expect(
      parseDiagnostics([JSON.stringify({ ...report, capture_available: false, log: '' })])
        .capture_available,
    ).toBe(false);
  });
  test('reports firmware errors and unsupported firmware', () => {
    expect(() =>
      parseDiagnostics(['{"kind":"diagnostics","ok":false,"error":"Out of memory"}']),
    ).toThrow('Out of memory');
    expect(() => parseDiagnostics(['Unrecognized command'])).toThrow('supports diagnostics');
  });
  test('rejects incomplete, oversized, or incompatible reports', () => {
    for (const invalid of [
      { kind: 'diagnostics' },
      { ...report, version: 2 },
      { ...report, dropped: -1 },
      { ...report, log: 'x'.repeat(16385) },
    ]) {
      expect(() => parseDiagnostics([JSON.stringify(invalid)])).toThrow(
        'invalid diagnostic report',
      );
    }
  });
});
