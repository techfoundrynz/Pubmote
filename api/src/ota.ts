import { graphql } from '@octokit/graphql';

export interface OtaEnv {
  RELEASES_AUTH_TOKEN?: string;
  OTA_RATE_LIMITER: { limit(options: { key: string }): Promise<{ success: boolean }> };
}

interface Release {
  tagName: string;
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
      tagName assets: releaseAssets(first: 50) { nodes { name downloadUrl } }
    }
    prerelease: releases(first: 2, orderBy: {field: CREATED_AT, direction: DESC}) {
      nodes { tagName assets: releaseAssets(first: 50) { nodes { name downloadUrl } } }
    }
  }
}`;

function selectAsset(release: Release | null, board: string) {
  if (!release) return null;
  const asset = release.assets.nodes.find(
    ({ name }) => name.startsWith(`${board}-`) && name.endsWith('.bin'),
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
  if (!board || !/^[a-z0-9_]{1,96}$/.test(board) || url.searchParams.size !== 1) {
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
    // One cache entry shared by every board; arbitrary board IDs cannot multiply GitHub calls.
    const cacheKey = new Request(`${url.origin}/ota/internal/releases-v1`);
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
    const stable = selectAsset(repository.stable, board);
    let prerelease = null;
    let nightly = null;
    for (const release of repository.prerelease.nodes) {
      const asset = selectAsset(release, board);
      if (release.tagName === 'nightly') nightly ??= asset;
      else prerelease ??= asset;
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
