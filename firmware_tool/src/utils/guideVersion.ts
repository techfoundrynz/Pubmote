const repository = 'techfoundrynz/Pubmote';

export function validGuideRef(ref: string): boolean {
  return (
    /^[a-zA-Z0-9][a-zA-Z0-9._/-]{0,127}$/.test(ref) &&
    !ref.includes('..') &&
    !ref.includes('//') &&
    !ref.endsWith('/')
  );
}

export function guideSourceUrl(ref: string): string {
  return `https://raw.githubusercontent.com/${repository}/${encodeURIComponent(ref)}/docs/quick-start.md`;
}

export function guideGithubUrl(ref: string): string {
  return `https://github.com/${repository}/blob/${encodeURIComponent(ref)}/docs/quick-start.md`;
}

// Resolve both older root-relative links and current relative links against the
// same tag as the Markdown. Screenshots must never drift to master either.
export function guideAssetUrl(href: string, ref: string, image: boolean): string {
  if (href === 'https://pubmote.com/getting-started/') {
    return `${href}?tag=${encodeURIComponent(ref)}`;
  }
  if (/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(href)) return href;
  const base = image
    ? `https://raw.githubusercontent.com/${repository}/${encodeURIComponent(ref)}/`
    : `https://github.com/${repository}/blob/${encodeURIComponent(ref)}/`;
  return href.startsWith('/') ? base + href.slice(1) : new URL(href, base + 'docs/').href;
}
