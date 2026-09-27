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
  const stored = new Map<string, Response>();
  vi.stubGlobal('caches', {
    default: {
      match: async (key: Request) => stored.get(key.url)?.clone(),
      put: async (key: Request, value: Response) => {
        stored.set(key.url, value.clone());
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

test('older firmware discovers and downloads exact-board symbols through the OTA service', async () => {
  cache();
  const historical = {
    tagName: 'v0.9.15',
    assets: {
      nodes: [
        {
          name: 'test_board_extra-v0.9.15.release.elf',
          downloadUrl:
            'https://github.com/techfoundrynz/Pubmote/releases/download/v0.9.15/test_board_extra-v0.9.15.release.elf',
        },
        {
          name: 'test_board-v0.9.15.release.elf',
          downloadUrl:
            'https://github.com/techfoundrynz/Pubmote/releases/download/v0.9.15/test_board-v0.9.15.release.elf',
        },
      ],
    },
  };
  const bytes = new Uint8Array([127, 69, 76, 70]);
  const fetchMock = vi
    .spyOn(globalThis, 'fetch')
    .mockResolvedValueOnce(Response.json({ data: { repository } }))
    .mockResolvedValueOnce(Response.json({ data: { repository: { release: historical } } }))
    .mockResolvedValueOnce(new Response(bytes));
  const symbols = await worker.fetch(
    new Request(`${base}/ota/v1/symbols?tag=v0.9.15&board=test_board`),
    env,
  );
  expect(symbols.status).toBe(200);
  expect(symbols.headers.get('Access-Control-Allow-Origin')).toBe('*');
  const info = (await symbols.json()) as { name: string; url: string };
  expect(info.name).toBe('test_board-v0.9.15.release.elf');
  const download = await worker.fetch(
    new Request(info.url, { headers: { Range: 'bytes=10-' } }),
    env,
  );
  expect(download.status).toBe(200);
  expect(download.headers.get('Content-Type')).toBe('application/octet-stream');
  expect(new Uint8Array(await download.arrayBuffer())).toEqual(bytes);
  expect(fetchMock).toHaveBeenCalledTimes(3);
  expect(fetchMock.mock.calls[2]).toEqual([
    historical.assets.nodes[1].downloadUrl,
    {
      redirect: 'follow',
      headers: { Accept: 'application/octet-stream' },
    },
  ]);
});

test('missing historical symbols fail without substituting another version', async () => {
  cache();
  vi.spyOn(globalThis, 'fetch')
    .mockResolvedValueOnce(Response.json({ data: { repository } }))
    .mockResolvedValueOnce(Response.json({ data: { repository: { release: null } } }));
  const response = await worker.fetch(
    new Request(`${base}/ota/v1/symbols?tag=v0.1&board=test_board`),
    env,
  );
  expect(response.status).toBe(404);
  expect(response.headers.get('Access-Control-Allow-Origin')).toBe('*');
});

test('the generic CORS proxy is removed', async () => {
  const fetchMock = vi.spyOn(globalThis, 'fetch');
  const response = await worker.fetch(
    new Request(`${base}/cors?https://github.com/techfoundrynz/Pubmote`),
    env,
  );
  expect(response.status).toBe(404);
  expect(fetchMock).not.toHaveBeenCalled();
});
