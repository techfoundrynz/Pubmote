// Work around Wrangler's unresponsive wildcard listener on this Windows host.
// Run Wrangler separately on 127.0.0.1:8792; firmware connects to LAN port 8791.
import http from 'node:http';

http
  .createServer((request, response) => {
    if (request.method !== 'GET' || !request.url?.startsWith('/pets/')) {
      response.writeHead(404).end('Not found');
      return;
    }
    const upstream = http.get(
      { hostname: '127.0.0.1', port: 8792, path: request.url },
      (result) => {
        response.writeHead(result.statusCode ?? 502, result.headers);
        result.pipe(response);
        result.on('error', () => response.destroy());
      },
    );
    upstream.setTimeout(45000, () => upstream.destroy(new Error('Worker timeout')));
    upstream.on('error', () => {
      if (!response.headersSent) response.writeHead(502).end('Local pet Worker unavailable');
      else response.destroy();
    });
    response.on('close', () => upstream.destroy());
  })
  .listen(8791, '0.0.0.0', () => {
    console.log('Pet LAN proxy listening on port 8791 → localhost:8792');
  });
