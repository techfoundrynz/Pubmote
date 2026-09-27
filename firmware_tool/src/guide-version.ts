import { Marked } from 'marked';
import DOMPurify from 'dompurify';
import { guideAssetUrl, guideGithubUrl, guideSourceUrl, validGuideRef } from './utils/guideVersion';

async function loadVersionedGuide() {
  const params = new URLSearchParams(window.location.search);
  if (!params.has('tag')) return;
  const ref = params.get('tag') ?? '';
  const main = document.querySelector('main');
  const status = document.getElementById('guide-status');
  const github = document.getElementById('github-guide') as HTMLAnchorElement | null;
  if (!main || !status || !github) return;

  // Never present today's instructions as if they belonged to an old release.
  main.hidden = true;
  if (!validGuideRef(ref)) {
    status.textContent = 'Invalid guide tag. Choose Current guide to read the latest instructions.';
    return;
  }
  status.textContent = `Loading guide for ${ref}…`;
  github.href = guideGithubUrl(ref);
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 15000);
  try {
    const response = await fetch(guideSourceUrl(ref), { signal: controller.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const markdown = new Marked({
      renderer: {
        image(token) {
          token.href = guideAssetUrl(token.href, ref, true);
          return false;
        },
        link(token) {
          token.href = guideAssetUrl(token.href, ref, false);
          return false;
        },
      },
    });
    main.innerHTML = DOMPurify.sanitize(await markdown.parse(await response.text()));
    for (const image of main.querySelectorAll('img')) {
      image.loading = 'lazy';
      image.decoding = 'async';
    }
    main.hidden = false;
    status.textContent = `Guide for ${ref}`;
    document.title = `Getting started — ${ref} — Pubmote`;
  } catch {
    status.textContent = `Could not load the guide for ${ref}. Check your connection or use View on GitHub. This version may predate the guide. Choose Current guide to read the latest instructions.`;
  } finally {
    window.clearTimeout(timeout);
  }
}

void loadVersionedGuide();
