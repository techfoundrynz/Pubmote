import { assert, test, vi, afterEach } from 'vitest';
import { otaResponse } from '../src/ota';

afterEach(() => vi.restoreAllMocks());

test('OTA manifest preserves channels, isolates boards and shares cached GitHub metadata', async () => {
  const board = 'test_board';
  const asset = (name: string) => ({
    name,
    downloadUrl: `https://github.com/techfoundrynz/Pubmote/releases/download/nightly/${name}`,
  });
  const release = (tagName: string, names: string[]) => ({
    tagName,
    assets: { nodes: names.map(asset) },
  });
  const repository = {
    stable: release('v1.0.0', [
      'test_board_extra-v1.bin',
      'test_board-v1.zip',
      'test_board-v1.bin',
    ]),
    prerelease: {
      nodes: [
        release('nightly', ['test_board-nightly.bin']),
        release('v1.1.0.dev', ['test_board-v1.1.bin']),
      ],
    },
  };
  let calls = 0;
  vi.spyOn(globalThis, 'fetch').mockImplementation(async (url, options) => {
    calls++;
    assert.equal(url, 'https://api.github.com/graphql');
    assert.equal(new Headers(options?.headers).get('authorization'), 'Bearer server-only');
    return Response.json({ data: { repository } });
  });
  const entries = new Map<string, Response>();
  const cache = {
    match: async (key: Request) => entries.get(key.url)?.clone(),
    put: async (key: Request, response: Response) => {
      entries.set(key.url, response.clone());
    },
  };
  const request = (board: string) =>
    new Request(`https://api.pubmote.com/ota/v1/releases?board=${board}`);
  const env = {
    RELEASES_AUTH_TOKEN: 'server-only',
    OTA_RATE_LIMITER: { limit: async () => ({ success: true }) },
  };
  const response = await otaResponse(request(board), env, cache);
  const text = await response.text();
  assert.equal(response.status, 200);
  assert.ok(text.length < 2048);
  assert.ok(!text.includes('server-only'));
  const manifest = JSON.parse(text);
  assert.equal(manifest.stable.tag, 'v1.0.0');
  assert.ok(manifest.stable.url.endsWith('/test_board-v1.bin'));
  assert.equal(manifest.prerelease.tag, 'v1.1.0.dev');
  assert.equal(manifest.nightly.tag, 'nightly');
  assert.deepEqual(await (await otaResponse(request('other_board'), env, cache)).json(), {
    stable: null,
    prerelease: null,
    nightly: null,
  });
  assert.equal(calls, 1);
});

test('OTA rejects invalid requests, upstream errors and unsafe assets without exposing credentials', async () => {
  let upstream = Response.json({ errors: [{ message: 'private details' }] });
  let writes = 0;
  vi.spyOn(globalThis, 'fetch').mockImplementation(async () => upstream.clone());
  const cache = {
    match: async () => undefined,
    put: async () => {
      writes++;
    },
  };
  const env = {
    RELEASES_AUTH_TOKEN: 'secret',
    OTA_RATE_LIMITER: { limit: async () => ({ success: true }) },
  };
  const request = (suffix = '?board=test_board', method = 'GET') =>
    new Request(`https://api.pubmote.com/ota/v1/releases${suffix}`, { method });
  assert.equal((await otaResponse(request('', 'POST'), env, cache)).status, 405);
  for (const suffix of [
    '',
    '?board=../bad',
    '?board=' + 'a'.repeat(97),
    '?board=test_board&board=other',
  ]) {
    assert.equal((await otaResponse(request(suffix), env, cache)).status, 400);
  }
  assert.equal(
    (await otaResponse(request(), { OTA_RATE_LIMITER: env.OTA_RATE_LIMITER }, cache)).status,
    503,
  );
  const failed = await otaResponse(request(), env, cache);
  assert.equal(failed.status, 502);
  assert.ok(!(await failed.text()).includes('private details'));
  assert.equal(writes, 0);
  upstream = new Response('rate limited', { status: 403 });
  assert.equal((await otaResponse(request(), env, cache)).status, 502);
  assert.equal(writes, 0);
  const base = 'https://github.com/techfoundrynz/Pubmote/releases/download/v1/';
  for (const downloadUrl of [
    'http://github.com/file.bin',
    'https://evil.example/file.bin',
    base + 'other_board-v1.bin',
    base + 'test_board-v1.bin?redirect=evil',
    base + 'test_board-v1.bin#fragment',
    'https://github.com/techfoundrynz/Pubmote/releases/download/v1/../v1/test_board-v1.bin',
  ]) {
    upstream = Response.json({
      data: {
        repository: {
          stable: {
            tagName: 'v1',
            assets: { nodes: [{ name: 'test_board-v1.bin', downloadUrl }] },
          },
          prerelease: { nodes: [] },
        },
      },
    });
    assert.equal((await otaResponse(request(), env, cache)).status, 502);
  }
});

test('rate limiting uses the Cloudflare client IP and stops requests before cache or GitHub access', async () => {
  let key: string | undefined;
  const env = {
    RELEASES_AUTH_TOKEN: 'secret',
    OTA_RATE_LIMITER: {
      limit: async (options: { key: string }) => {
        key = options.key;
        return { success: false };
      },
    },
  };
  const cache = {
    put: async () => {},
    match: async () => {
      assert.fail('Rate-limited request reached the cache');
    },
  };
  const response = await otaResponse(
    new Request('https://api.pubmote.com/ota/v1/releases?board=test_board', {
      headers: { 'CF-Connecting-IP': '192.0.2.1', 'X-Forwarded-For': 'spoofed' },
    }),
    env,
    cache,
  );
  assert.equal(key, 'ota:192.0.2.1');
  assert.equal(response.status, 429);
  assert.equal(response.headers.get('Retry-After'), '60');
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
});

test('rate limiter failure returns an error without contacting GitHub', async () => {
  const env = {
    RELEASES_AUTH_TOKEN: 'secret',
    OTA_RATE_LIMITER: {
      limit: async () => {
        throw new Error('unavailable');
      },
    },
  };
  const cache = {
    put: async () => {},
    match: async () => {
      assert.fail('Limiter failure reached cache');
    },
  };
  const response = await otaResponse(
    new Request('https://api.pubmote.com/ota/v1/releases?board=test_board'),
    env,
    cache,
  );
  assert.equal(response.status, 502);
});
