import assert from 'node:assert/strict';
import { test } from 'node:test';
import { PhotonImage } from '@cf-wasm/photon';
import { packPet, petsResponse } from '../src/pets.ts';

for (const height of [1872, 2288]) {
  test(`packs ${height === 1872 ? 'v1' : 'v2'} atlas with transparency and correct frame boundaries`, () => {
    const pixels = new Uint8Array(1536 * height * 4);
    for (let y = 0; y < height; y++)
      for (let x = 0; x < 1536; x++) {
        pixels.set(
          [Math.floor(x / 192) * 20, Math.floor(y / 208) * 20, 123, 127],
          (y * 1536 + x) * 4,
        );
      }
    const image = new PhotonImage(pixels, 1536, height);
    try {
      const packed = packPet(image.get_bytes());
      const preview = packPet(image.get_bytes(), true);
      assert.equal(preview.length, 59920);
      assert.deepEqual(
        [...preview.slice(0, 16)],
        [80, 77, 80, 82, 69, 86, 49, 0, 48, 0, 52, 0, 6, 0, 4, 1],
      );
      for (let frame = 0; frame < 6; frame++) {
        const offset = 16 + (26 * 288 + frame * 48 + 24) * 4;
        assert.deepEqual([...preview.slice(offset, offset + 4)], [frame * 20, 0, 123, 127]);
      }
      assert.equal(packed.length, 718864);
      assert.deepEqual(
        [...packed.slice(0, 16)],
        [80, 77, 80, 69, 84, 48, 49, 0, 48, 0, 52, 0, 9, 0, 4, 1],
      );
      for (let row = 0; row < 9; row++)
        for (let col = 0; col < 8; col++) {
          const offset = 16 + ((row * 52 + 26) * 384 + col * 48 + 24) * 4;
          assert.deepEqual([...packed.slice(offset, offset + 4)], [col * 20, row * 20, 123, 127]);
        }
    } finally {
      image.free();
    }
  });
}

test('rejects unsupported sheet dimensions', () => {
  const image = new PhotonImage(new Uint8Array(16), 2, 2);
  try {
    assert.throws(() => packPet(image.get_bytes()), /dimensions/);
  } finally {
    image.free();
  }
});

test('validates routes, pagination, upstream errors and sprite origins', async (t) => {
  let response = Response.json({
    pets: [
      { id: 'pingu', displayName: 'Pingu' },
      { id: '../bad', displayName: 'Bad' },
    ],
    totalPages: 3,
  });
  const calls = [];
  t.mock.method(globalThis, 'fetch', async (url) => {
    calls.push(String(url));
    return response.clone();
  });
  const request = (path, method = 'GET') =>
    petsResponse(new Request('https://api.pubmote.com' + path, { method }));
  assert.equal((await request('/pets/catalog?page=-1')).status, 400);
  assert.equal((await request('/pets/catalog', 'POST')).status, 405);
  assert.equal((await request('/pets/unknown')).status, 404);
  assert.equal((await request('/pets/catalog?pageSize=2')).status, 400);
  assert.equal(calls.length, 0);
  const catalog = await (await request('/pets/catalog?page=2')).json();
  assert.deepEqual(catalog, { page: 2, pages: 3, pets: [{ id: 'pingu', name: 'Pingu' }] });
  assert.match(calls[0], /page=2&pageSize=4/);
  const single = await (await request('/pets/catalog?page=3&pageSize=1')).json();
  assert.deepEqual(single, { page: 3, pages: 3, pets: [{ id: 'pingu', name: 'Pingu' }] });
  assert.match(calls.at(-1), /page=3&pageSize=1/);
  response = new Response('Unavailable', { status: 503 });
  assert.equal((await request('/pets/catalog')).status, 502);
  response = Response.json({ pet: { spritesheetUrl: 'https://example.org/private' } });
  const before = calls.length;
  assert.equal((await request('/pets/pingu.pet')).status, 502);
  assert.equal(calls.length, before + 1);
  response = new Response('x', { headers: { 'Content-Length': String(129 * 1024) } });
  assert.equal((await request('/pets/catalog')).status, 502);
});
