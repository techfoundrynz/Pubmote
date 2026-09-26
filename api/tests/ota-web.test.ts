import { afterEach, expect, test, vi } from 'vitest';
import worker from '../src/index';

const base = 'https://api.pubmote.com';
const env = {
  RELEASES_AUTH_TOKEN: 'secret',
  OTA_RATE_LIMITER: { limit: async () => ({ success: true }) },
};
function release(tagName: string, isPrerelease = false) {
  return {
    tagName,
    isPrerelease,
    publishedAt: '2026-09-01T00:00:00Z',
    assets: {
      nodes: ['test_board-v1.bin', 'test_board-v1.zip', 'other_board-v1.zip'].map((name) => ({
        name,
        downloadUrl: `https://github.com/techfoundrynz/Pubmote/releases/download/${tagName}/${name}`,
      })),
    },
  };
}
const repository = {
  stable: release('v1'),
  prerelease: { nodes: [release('nightly', true), release('v2', true)] },
};
function cache() {
  let stored: Response | undefined;
  vi.stubGlobal('caches', {
    default: {
      match: async () => stored?.clone(),
      put: async (_: Request, value: Response) => {
        stored = value.clone();
      },
    },
  });
}
afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

test('web and firmware share cached releases, with ZIP routes for every board', async () => {
  cache();
  const fetchMock = vi
    .spyOn(globalThis, 'fetch')
    .mockResolvedValue(Response.json({ data: { repository } }));
  const web = await worker.fetch(new Request(`${base}/ota/v1/releases?format=web`), env);
  expect(web.headers.get('Access-Control-Allow-Origin')).toBe('*');
  const versions = (await web.json()) as {
    releaseType: string;
    variants: { zipUrl: string; variant: string }[];
  }[];
  expect(versions.map((v) => v.releaseType)).toEqual(['release', 'prerelease', 'nightly']);
  expect(versions[0].variants.map((v) => v.variant)).toEqual(['test_board', 'other_board']);
  expect(versions[0].variants[0].zipUrl).toBe(
    `${base}/ota/v1/download?tag=v1&asset=test_board-v1.zip`,
  );
  const firmware = await worker.fetch(new Request(`${base}/ota/v1/releases?board=test_board`), env);
  expect(await firmware.json()).toEqual({
    stable: { tag: 'v1', url: repository.stable.assets.nodes[0].downloadUrl },
    prerelease: { tag: 'v2', url: repository.prerelease.nodes[1].assets.nodes[0].downloadUrl },
    nightly: { tag: 'nightly', url: repository.prerelease.nodes[0].assets.nodes[0].downloadUrl },
  });
  expect(fetchMock).toHaveBeenCalledTimes(1);
});

test('package downloads stream bytes without forwarding ranges, cookies or credentials', async () => {
  cache();
  const bytes = new Uint8Array([80, 75, 3, 4, 0, 255]);
  const fetchMock = vi
    .spyOn(globalThis, 'fetch')
    .mockResolvedValueOnce(Response.json({ data: { repository } }))
    .mockResolvedValueOnce(new Response(bytes));
  const response = await worker.fetch(
    new Request(`${base}/ota/v1/download?tag=v1&asset=test_board-v1.zip`, {
      headers: {
        Origin: 'https://pubmote.com',
        Range: 'bytes=4-',
        Cookie: 'private',
        Authorization: 'private',
        'If-None-Match': 'old',
      },
    }),
    env,
  );
  expect(response.status).toBe(200);
  expect(new Uint8Array(await response.arrayBuffer())).toEqual(bytes);
  expect(response.headers.get('Cache-Control')).toBe('no-store');
  expect(response.headers.get('Access-Control-Allow-Origin')).toBe('*');
  expect(fetchMock.mock.calls[1]).toEqual([
    repository.stable.assets.nodes[1].downloadUrl,
    {
      redirect: 'follow',
      headers: { Accept: 'application/octet-stream' },
    },
  ]);
});

test('partial upstream ZIPs fail with a readable CORS error', async () => {
  cache();
  vi.spyOn(globalThis, 'fetch')
    .mockResolvedValueOnce(Response.json({ data: { repository } }))
    .mockResolvedValueOnce(new Response('partial', { status: 206 }));
  const response = await worker.fetch(
    new Request(`${base}/ota/v1/download?tag=v1&asset=test_board-v1.zip`),
    env,
  );
  expect(response.status).toBe(502);
  expect(response.headers.get('Access-Control-Allow-Origin')).toBe('*');
  expect(await response.text()).toContain('complete package');
});

test('download requests cannot select arbitrary URLs or absent assets', async () => {
  cache();
  const fetchMock = vi
    .spyOn(globalThis, 'fetch')
    .mockResolvedValue(Response.json({ data: { repository } }));
  for (const suffix of [
    '?tag=v1&asset=../evil.zip',
    '?tag=v1&asset=test_board-v1.zip&url=https://evil.example',
  ]) {
    expect((await worker.fetch(new Request(`${base}/ota/v1/download${suffix}`), env)).status).toBe(
      400,
    );
  }
  expect(fetchMock).not.toHaveBeenCalled();
  expect(
    (await worker.fetch(new Request(`${base}/ota/v1/download?tag=v1&asset=missing-v1.zip`), env))
      .status,
  ).toBe(404);
  expect(fetchMock).toHaveBeenCalledTimes(1);
});

test('OTA error responses are readable by the web tool', async () => {
  cache();
  const response = await worker.fetch(new Request(`${base}/ota/v1/releases?format=web`), {
    ...env,
    RELEASES_AUTH_TOKEN: undefined,
  });
  expect(response.status).toBe(503);
  expect(response.headers.get('Access-Control-Allow-Origin')).toBe('*');
});

test('generic proxy varies CORS responses by origin', async () => {
  vi.spyOn(globalThis, 'fetch').mockResolvedValue(
    new Response('ok', { headers: { Vary: 'Accept-Encoding' } }),
  );
  const response = await worker.fetch(
    new Request(`${base}/cors?https://github.com/techfoundrynz/Pubmote`, {
      headers: { Origin: 'https://pubmote.com' },
    }),
    env,
  );
  expect(response.headers.get('Vary')).toBe('Accept-Encoding, Origin');
});

test('stable releases are not also advertised as prereleases', async () => {
  cache();
  vi.spyOn(globalThis, 'fetch').mockResolvedValue(
    Response.json({
      data: {
        repository: {
          stable: repository.stable,
          prerelease: { nodes: [repository.stable, release('nightly', true)] },
        },
      },
    }),
  );
  const web = await worker.fetch(new Request(`${base}/ota/v1/releases?format=web`), env);
  expect(((await web.json()) as { releaseType: string }[]).map((v) => v.releaseType)).toEqual([
    'release',
    'nightly',
  ]);
  const firmware = await worker.fetch(new Request(`${base}/ota/v1/releases?board=test_board`), env);
  expect(((await firmware.json()) as { prerelease: unknown }).prerelease).toBeNull();
});
