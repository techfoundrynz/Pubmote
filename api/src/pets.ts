import { PhotonImage, SamplingFilter, crop, resize } from '@cf-wasm/photon';

const SOURCE = 'https://codex-pets.net';
const ID = /^[a-zA-Z0-9_-]{1,80}$/;
const MAX_IMAGE = 20 * 1024 * 1024;

async function readBounded(response: Response, max: number): Promise<Uint8Array> {
  if (!response.ok || !response.body) throw new Error('Pet source unavailable');
  if (Number(response.headers.get('Content-Length')) > max)
    throw new Error('Pet response too large');
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let size = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.length;
      if (size > max) throw new Error('Pet response too large');
      chunks.push(value);
    }
  } finally {
    await reader.cancel();
  }
  const bytes = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.length;
  }
  return bytes;
}

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value))
    throw new Error('Invalid pet metadata');
  return value as Record<string, unknown>;
}

async function sourceJson(path: string): Promise<Record<string, unknown>> {
  const response = await fetch(SOURCE + path, {
    redirect: 'manual',
    signal: AbortSignal.timeout(15000),
  });
  return record(JSON.parse(new TextDecoder().decode(await readBounded(response, 128 * 1024))));
}

// Version 1 wire format: 16-byte header then a 384x468 straight-alpha RGBA atlas.
// Retain the nine shared animation rows from both Codex sprite versions.
export function packPet(bytes: Uint8Array, preview = false): Uint8Array {
  const input = PhotonImage.new_from_byteslice(bytes);
  let clipped: PhotonImage | undefined;
  let small: PhotonImage | undefined;
  try {
    if (input.get_width() !== 1536 || ![1872, 2288].includes(input.get_height())) {
      throw new Error('Unsupported pet sheet dimensions');
    }
    clipped = crop(input, 0, 0, preview ? 1152 : 1536, preview ? 208 : 1872);
    small = resize(clipped, preview ? 288 : 384, preview ? 52 : 468, SamplingFilter.Nearest);
    const pixels = small.get_raw_pixels();
    const output = new Uint8Array(16 + pixels.length);
    output.set(new TextEncoder().encode(preview ? 'PMPREV1\0' : 'PMPET01\0'));
    const header = new DataView(output.buffer);
    header.setUint16(8, 48, true);
    header.setUint16(10, 52, true);
    header.setUint16(12, preview ? 6 : 9, true);
    header.setUint16(14, 260, true);
    output.set(pixels, 16);
    return output;
  } finally {
    small?.free();
    clipped?.free();
    input.free();
  }
}

export async function petsResponse(request: Request): Promise<Response> {
  if (request.method !== 'GET') return new Response('Method not allowed', { status: 405 });
  const url = new URL(request.url);
  try {
    if (url.pathname === '/pets/catalog') {
      const page = Number(url.searchParams.get('page') || '1');
      const pageSize = Number(url.searchParams.get('pageSize') || '4');
      if (![1, 4].includes(pageSize)) return new Response('Invalid page size', { status: 400 });
      if (!Number.isInteger(page) || page < 1 || page > 10000)
        return new Response('Invalid page', { status: 400 });
      const data = await sourceJson(`/api/pets?page=${page}&pageSize=${pageSize}`);
      if (
        !Array.isArray(data.pets) ||
        typeof data.totalPages !== 'number' ||
        !Number.isInteger(data.totalPages) ||
        data.totalPages < 0 ||
        data.totalPages > 10000
      )
        throw new Error('Invalid catalog');
      const pets = data.pets
        .slice(0, pageSize)
        .map(record)
        .filter((pet) => typeof pet.id === 'string' && ID.test(pet.id))
        .map((pet) => {
          // MCU fonts embed a small charset; preserve readable names without missing glyphs.
          const name = String(pet.displayName || pet.id)
            .normalize('NFKD')
            .replace(/[\u0300-\u036f]/g, '');
          return {
            id: pet.id,
            name: (/^[\x20-\x7e]+$/.test(name) ? name : String(pet.id)).slice(0, 64),
          };
        });
      return Response.json(
        { page, pages: data.totalPages, pets },
        { headers: { 'Cache-Control': 'public, max-age=60' } },
      );
    }
    const match = /^\/pets\/([a-zA-Z0-9_-]{1,80})\.(pet|preview)$/.exec(url.pathname);
    if (!match) return new Response('Not found', { status: 404 });
    const detail = await sourceJson(`/api/pets/${encodeURIComponent(match[1])}`);
    const pet = record(detail.pet);
    if (typeof pet.spritesheetUrl !== 'string') throw new Error('Missing sprite source');
    const sheet = new URL(pet.spritesheetUrl, SOURCE);
    if (sheet.origin !== SOURCE || !sheet.pathname.startsWith('/assets/pets/'))
      throw new Error('Invalid sprite source');
    const response = await fetch(sheet, { redirect: 'manual', signal: AbortSignal.timeout(20000) });
    const packed = packPet(await readBounded(response, MAX_IMAGE), match[2] === 'preview');
    return new Response(packed, {
      headers: {
        'Content-Type': 'application/octet-stream',
        'Cache-Control': 'public, max-age=86400',
      },
    });
  } catch (error) {
    console.error('Pet request failed', error instanceof Error ? error.message : 'Unknown error');
    return Response.json(
      { error: 'Could not load this pet from codex-pets.net. Please retry.' },
      { status: 502 },
    );
  }
}
