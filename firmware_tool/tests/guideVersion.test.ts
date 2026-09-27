import { describe, expect, it } from 'vitest';
import {
  guideAssetUrl,
  guideGithubUrl,
  guideSourceUrl,
  validGuideRef,
} from '../src/utils/guideVersion';

describe('versioned getting-started guide', () => {
  it('accepts release tags, custom tags, and immutable commit IDs', () => {
    for (const ref of ['v0.9.10', 'release/v1.0.0', 'a'.repeat(40)]) {
      expect(validGuideRef(ref)).toBe(true);
      expect(guideSourceUrl(ref)).toContain(`/${encodeURIComponent(ref)}/docs/quick-start.md`);
      expect(guideGithubUrl(ref)).toContain(`/blob/${encodeURIComponent(ref)}/`);
    }
  });
  it('rejects malformed or path-traversing refs', () => {
    for (const ref of ['', '../master', 'v1/../../master', 'v1?x=1', '<script>', 'v1//x', 'v1/']) {
      expect(validGuideRef(ref)).toBe(false);
    }
  });
  it('pins images and internal links to the requested version', () => {
    expect(guideAssetUrl('configure_ve_wifi.png', 'v1.2.3', true)).toBe(
      'https://raw.githubusercontent.com/techfoundrynz/Pubmote/v1.2.3/docs/configure_ve_wifi.png',
    );
    for (const href of ['/README.md#hardware', '../README.md#hardware']) {
      expect(guideAssetUrl(href, 'v1.2.3', false)).toBe(
        'https://github.com/techfoundrynz/Pubmote/blob/v1.2.3/README.md#hardware',
      );
    }
    expect(guideAssetUrl('builds/leaf-blaster.md', 'v1.2.3', false)).toContain(
      '/v1.2.3/docs/builds/',
    );
    expect(guideAssetUrl('https://pubmote.com/', 'v1.2.3', false)).toBe('https://pubmote.com/');
    expect(guideAssetUrl('#pairing', 'v1.2.3', false)).toBe('#pairing');
    expect(guideAssetUrl('https://pubmote.com/getting-started/', 'v1.2.3', false)).toBe(
      'https://pubmote.com/getting-started/?tag=v1.2.3',
    );
  });
});
