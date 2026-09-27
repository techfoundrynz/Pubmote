import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Marked } from 'marked';
import type { Plugin } from 'vite';

const docs = resolve(dirname(fileURLToPath(import.meta.url)), '../docs');
const source = resolve(docs, 'quick-start.md');
const github = 'https://github.com/techfoundrynz/Pubmote/blob/master/';

// Render trusted repository Markdown at build time. The resulting page works
// on GitHub Pages without a router, JavaScript, or a connected USB device.
export function guidePage(): Plugin {
  let isBuild = false;
  let scriptReference = '';
  const assets = new Map<string, Buffer>();
  const markdown = new Marked({
    renderer: {
      image({ href, text }) {
        const name = href.replace(/^\.\//, '');
        if (!/^[\w-]+\.(png|gif)$/.test(name)) {
          throw new Error(`Unsupported guide image: ${href}`);
        }
        assets.set(name, readFileSync(resolve(docs, name)));
        return `<img src="./${name}" alt="${text.replaceAll('"', '&quot;')}" loading="lazy">`;
      },
      link(token) {
        if (token.href.startsWith('../')) token.href = github + token.href.slice(3);
        else if (token.href.startsWith('/')) token.href = github + token.href.slice(1);
        else if (!/^(https?:|#)/.test(token.href)) token.href = github + 'docs/' + token.href;
        return false;
      },
    },
  });
  function render(scriptUrl = '/src/guide-version.ts') {
    assets.clear();
    const body = markdown.parse(readFileSync(source, 'utf8'));
    return `<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="Set up your Pubmote: flash firmware, calibrate the joystick, pair your board, and troubleshoot connections.">
<title>Getting started with Pubmote</title>
<style>
:root { color-scheme: light dark; font-family: system-ui, sans-serif; line-height: 1.65; }
body { max-width: 50rem; margin: auto; padding: 1.25rem; overflow-wrap: break-word; }
nav { display: flex; flex-wrap: wrap; gap: 1rem; padding: .5rem 0 1.5rem; border-bottom: 1px solid #888; }
a { color: light-dark(#075ca8, #80c4ff); }
h1,h2,h3 { line-height: 1.25; margin-top: 1.75em; }
li { margin-block: .5rem; } img { display: block; max-width: 100%; height: auto; margin: 1.5rem auto; }
blockquote { margin-inline: 0; padding-left: 1rem; border-left: 3px solid #888; }
</style></head><body>
<nav aria-label="Guide navigation"><a href="../">Firmware tool</a><a id="github-guide" href="${github}docs/quick-start.md">View on GitHub</a><a href="./">Current guide</a></nav>
<p id="guide-status" role="status">Current guide</p>
<noscript>Versioned guides need JavaScript. The content below is the current guide.</noscript>
<main>${body}</main><script type="module" src="${scriptUrl}"></script></body></html>`;
  }
  return {
    name: 'getting-started-guide',
    configResolved(config) {
      isBuild = config.command === 'build';
    },
    buildStart() {
      this.addWatchFile(source);
      if (isBuild) {
        scriptReference = this.emitFile({
          type: 'chunk',
          id: resolve(docs, '../firmware_tool/src/guide-version.ts'),
        });
      }
    },
    generateBundle() {
      this.emitFile({
        type: 'asset',
        fileName: 'getting-started/index.html',
        source: render(`../${this.getFileName(scriptReference)}`),
      });
      for (const [name, content] of assets) {
        this.addWatchFile(resolve(docs, name));
        this.emitFile({ type: 'asset', fileName: `getting-started/${name}`, source: content });
      }
    },
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const path = req.url?.split('?')[0];
        if (path === '/getting-started') {
          res.writeHead(302, { Location: '/getting-started/' + req.url!.slice(path.length) });
          res.end();
        } else if (path === '/getting-started/' || path === '/getting-started/index.html') {
          res.setHeader('Content-Type', 'text/html; charset=utf-8');
          res.end(render());
        } else if (path?.startsWith('/getting-started/')) {
          render();
          const name = path.slice('/getting-started/'.length);
          const content = assets.get(name);
          if (!content) return next();
          res.setHeader('Content-Type', name.endsWith('.gif') ? 'image/gif' : 'image/png');
          res.end(content);
        } else next();
      });
    },
  };
}
