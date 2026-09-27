import { graphql } from '@octokit/graphql';

export interface OtaEnv {
  RELEASES_AUTH_TOKEN?: string;
  OTA_RATE_LIMITER: { limit(options: { key: string }): Promise<{ success: boolean }> };
}

interface Release {
  tagName: string;
  publishedAt?: string;
  isPrerelease?: boolean;
  assets: { nodes: { name: string; downloadUrl: string }[] };
}

interface Repository {
  stable: Release | null;
  prerelease: { nodes: Release[] };
}

// Preserve the firmware's release ordering and its mutable `nightly` tag.
const QUERY = `{
  repository(owner: "techfoundrynz", name: "Pubmote") {
    stable: latestRelease {
      tagName publishedAt isPrerelease assets: releaseAssets(first: 50) { nodes { name downloadUrl } }
    }
    prerelease: releases(first: 2, orderBy: {field: CREATED_AT, direction: DESC}) {
      nodes { tagName publishedAt isPrerelease assets: releaseAssets(first: 50) { nodes { name downloadUrl } } }
    }
  }
}`;

function selectAsset(release: Release | null, board: string, extension = '.bin') {
  if (!release) return null;
  const asset = release.assets.nodes.find(
    ({ name }) => name.startsWith(`${board}-`) && name.endsWith(extension),
  );
  if (!asset) return null;
  // Match the firmware's fixed-size fields; never return truncated metadata.
  if (!/^[\x20-\x7e]{1,31}$/.test(release.tagName)) throw new Error('Invalid tag');
  const url = new URL(asset.downloadUrl);
  if (
    asset.downloadUrl !== url.href ||
    url.protocol !== 'https:' ||
    url.hostname !== 'github.com' ||
    !url.pathname.toLowerCase().startsWith('/techfoundrynz/pubmote/releases/download/') ||
    url.username ||
    url.password ||
    url.port ||
    url.search ||
    url.hash ||
    decodeURIComponent(url.pathname.split('/').pop() ?? '') !== asset.name ||
    asset.downloadUrl.length > 511
  )
    throw new Error('Invalid asset URL');
  return { tag: release.tagName, url: asset.downloadUrl };
}

async function getRepository(
  origin: string,
  env: OtaEnv,
  cache: Pick<Cache, 'match' | 'put'>,
): Promise<Repository> {
  // One cache entry shared by every board; arbitrary board IDs cannot multiply GitHub calls.
  const cacheKey = new Request(`${origin}/ota/internal/releases-v2`);
  const response = await cache.match(cacheKey);
  let repository: Repository;
  if (response) {
    repository = await response.json<Repository>();
  } else {
    const result = await graphql<{ repository: Repository | null }>(QUERY, {
      headers: {
        authorization: `Bearer ${env.RELEASES_AUTH_TOKEN}`,
        'user-agent': 'Pubmote-OTA',
      },
      request: { fetch, signal: AbortSignal.timeout(10000) },
    });
    if (!result.repository) throw new Error('GitHub repository unavailable');
    repository = result.repository;
    await cache.put(
      cacheKey,
      Response.json(repository, { headers: { 'Cache-Control': 'public, max-age=60' } }),
    );
  }
  return repository;
}

export async function otaResponse(
  request: Request,
  env: OtaEnv,
  cache: Pick<Cache, 'match' | 'put'>,
): Promise<Response> {
  if (request.method !== 'GET') {
    return new Response('Method not allowed', { status: 405, headers: { Allow: 'GET' } });
  }
  const url = new URL(request.url);
  const board = url.searchParams.get('board');
  const web = url.searchParams.get('format') === 'web' && url.searchParams.size === 1;
  if (!web && (!board || !/^[a-z0-9_]{1,96}$/.test(board) || url.searchParams.size !== 1)) {
    return Response.json({ error: 'Invalid board' }, { status: 400 });
  }
  if (!env.RELEASES_AUTH_TOKEN) {
    return Response.json({ error: 'OTA service is not configured' }, { status: 503 });
  }
  try {
    // Cloudflare supplies this header; never use caller-controlled X-Forwarded-For.
    const ip = request.headers.get('CF-Connecting-IP') ?? 'local';
    const { success } = await env.OTA_RATE_LIMITER.limit({ key: `ota:${ip}` });
    if (!success) {
      return Response.json(
        { error: 'Too many update checks' },
        {
          status: 429,
          headers: { 'Retry-After': '60', 'Cache-Control': 'no-store' },
        },
      );
    }
    const repository = await getRepository(url.origin, env, cache);
    if (web) {
      const channels = [
        { releaseType: 'release', release: repository.stable },
        {
          releaseType: 'prerelease',
          release: repository.prerelease.nodes.find(
            (r) => r.tagName !== 'nightly' && r.isPrerelease !== false,
          ),
        },
        {
          releaseType: 'nightly',
          release: repository.prerelease.nodes.find((r) => r.tagName === 'nightly'),
        },
      ];
      return Response.json(
        channels.flatMap(({ releaseType, release }) => {
          if (!release) return [];
          const variants = release.assets.nodes.flatMap((file) => {
            const { name } = file;
            const board = name.split('-')[0];
            if (!/^[a-z0-9_]{1,96}$/.test(board) || !name.endsWith('.zip')) return [];
            const asset = selectAsset({ ...release, assets: { nodes: [file] } }, board, '.zip');
            if (!asset) return [];
            const download = new URL('/ota/v1/download', url.origin);
            download.searchParams.set('tag', release.tagName);
            download.searchParams.set('asset', name);
            return [{ variant: board, date: release.publishedAt ?? '', zipUrl: download.href }];
          });
          return variants.length
            ? [{ version: release.tagName, date: release.publishedAt ?? '', releaseType, variants }]
            : [];
        }),
      );
    }
    const stable = selectAsset(repository.stable, board!);
    let prerelease = null;
    let nightly = null;
    for (const release of repository.prerelease.nodes) {
      const asset = selectAsset(release, board!);
      if (release.tagName === 'nightly') nightly ??= asset;
      else if (release.isPrerelease !== false) prerelease ??= asset;
    }
    const manifest = JSON.stringify({ stable, prerelease, nightly });
    if (new TextEncoder().encode(manifest).length >= 2048) throw new Error('Manifest too large');
    return new Response(manifest, {
      headers: {
        'Content-Type': 'application/json',
        'X-Content-Type-Options': 'nosniff',
        'Cache-Control': 'no-store',
        'Access-Control-Allow-Origin': '*',
      },
    });
  } catch {
    return Response.json({ error: 'Unable to fetch firmware releases' }, { status: 502 });
  }
}

async function getReleaseByTag(
  tag: string,
  origin: string,
  env: OtaEnv,
  cache: Pick<Cache, 'match' | 'put'>,
): Promise<Release | null> {
  const repository = await getRepository(origin, env, cache);
  const current = [repository.stable, ...repository.prerelease.nodes].find(
    (r) => r?.tagName === tag,
  );
  if (current) return current;
  // Devices may still run an older release whose symbols are absent from the latest channels.
  const key = new Request(`${origin}/ota/internal/tag/${encodeURIComponent(tag)}`);
  const cached = await cache.match(key);
  if (cached) return cached.json<Release | null>();
  const result = await graphql<{ repository: { release: Release | null } | null }>(
    `
      query ($tag: String!) {
        repository(owner: "techfoundrynz", name: "Pubmote") {
          release(tagName: $tag) {
            tagName
            assets: releaseAssets(first: 50) {
              nodes {
                name
                downloadUrl
              }
            }
          }
        }
      }
    `,
    {
      tag,
      headers: { authorization: `Bearer ${env.RELEASES_AUTH_TOKEN}`, 'user-agent': 'Pubmote-OTA' },
      request: { fetch, signal: AbortSignal.timeout(10000) },
    },
  );
  if (!result.repository) throw new Error('GitHub repository unavailable');
  const release = result.repository.release;
  await cache.put(
    key,
    Response.json(release, { headers: { 'Cache-Control': 'public, max-age=60' } }),
  );
  return release;
}

export async function otaDownloadResponse(
  request: Request,
  env: OtaEnv,
  cache: Pick<Cache, 'match' | 'put'>,
): Promise<Response> {
  if (request.method !== 'GET')
    return new Response('Method not allowed', { status: 405, headers: { Allow: 'GET' } });
  const url = new URL(request.url);
  const tag = url.searchParams.get('tag');
  const name = url.searchParams.get('asset');
  if (
    url.searchParams.size !== 2 ||
    !tag ||
    !/^[a-zA-Z0-9_.-]{1,31}$/.test(tag) ||
    !name ||
    !/^[a-z0-9_]{1,96}-[a-zA-Z0-9_.-]+\.(zip|elf)$/.test(name)
  ) {
    return Response.json({ error: 'Invalid package' }, { status: 400 });
  }
  if (!env.RELEASES_AUTH_TOKEN)
    return Response.json({ error: 'OTA service is not configured' }, { status: 503 });
  try {
    const ip = request.headers.get('CF-Connecting-IP') ?? 'local';
    if (!(await env.OTA_RATE_LIMITER.limit({ key: `ota:${ip}` })).success) {
      return new Response('Too many downloads', { status: 429, headers: { 'Retry-After': '60' } });
    }
    const release = await getReleaseByTag(tag, url.origin, env, cache);
    const asset = release?.assets.nodes.find((a) => a.name === name);
    if (!release || !asset) return new Response('Package not found', { status: 404 });
    const selected = selectAsset(
      { ...release, assets: { nodes: [asset] } },
      name.split('-')[0],
      name.endsWith('.elf') ? '.elf' : '.zip',
    );
    if (!selected) return new Response('Package not found', { status: 404 });
    // Forward neither browser range/cache headers nor our GitHub API credential.
    const upstream = await fetch(selected.url, {
      redirect: 'follow',
      headers: { Accept: 'application/octet-stream' },
    });
    if (upstream.status !== 200) {
      await upstream.body?.cancel();
      return new Response('Unable to download complete package', { status: 502 });
    }
    const headers = new Headers({
      'Content-Type': name.endsWith('.elf') ? 'application/octet-stream' : 'application/zip',
      'Content-Disposition': `attachment; filename="${name}"`,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
    });
    return new Response(upstream.body, { headers });
  } catch {
    return Response.json({ error: 'Unable to download package' }, { status: 502 });
  }
}

export async function otaSymbolsResponse(
  request: Request,
  env: OtaEnv,
  cache: Pick<Cache, 'match' | 'put'>,
): Promise<Response> {
  if (request.method !== 'GET')
    return new Response('Method not allowed', { status: 405, headers: { Allow: 'GET' } });
  const url = new URL(request.url);
  const tag = url.searchParams.get('tag');
  const board = url.searchParams.get('board');
  if (
    url.searchParams.size !== 2 ||
    !tag ||
    !/^[a-zA-Z0-9_.-]{1,31}$/.test(tag) ||
    !board ||
    !/^[a-z0-9_]{1,96}$/.test(board)
  ) {
    return Response.json({ error: 'Invalid symbol request' }, { status: 400 });
  }
  if (!env.RELEASES_AUTH_TOKEN)
    return new Response('OTA service is not configured', { status: 503 });
  try {
    const ip = request.headers.get('CF-Connecting-IP') ?? 'local';
    if (!(await env.OTA_RATE_LIMITER.limit({ key: `ota:${ip}` })).success)
      return new Response('Too many requests', { status: 429, headers: { 'Retry-After': '60' } });
    const release = await getReleaseByTag(tag, url.origin, env, cache);
    const asset = selectAsset(release, board, '.elf');
    if (!asset) return new Response('Matching debug symbols not found', { status: 404 });
    const name = decodeURIComponent(new URL(asset.url).pathname.split('/').pop()!);
    const download = new URL('/ota/v1/download', url.origin);
    download.searchParams.set('tag', tag);
    download.searchParams.set('asset', name);
    return Response.json({ name, url: download.href, githubUrl: asset.url });
  } catch {
    return new Response('Unable to find debug symbols', { status: 502 });
  }
}
