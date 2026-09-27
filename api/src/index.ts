import { otaResponse, otaDownloadResponse, otaSymbolsResponse, type OtaEnv } from './ota';

export default {
  async fetch(request: Request, env: OtaEnv): Promise<Response> {
    const path = new URL(request.url).pathname;
    const handler =
      path === '/ota/v1/releases'
        ? otaResponse
        : path === '/ota/v1/download'
          ? otaDownloadResponse
          : path === '/ota/v1/symbols'
            ? otaSymbolsResponse
            : null;
    if (!handler) return new Response('Not Found', { status: 404 });
    const response = await handler(request, env, caches.default);
    const headers = new Headers(response.headers);
    headers.set('Access-Control-Allow-Origin', '*');
    headers.set('Cache-Control', 'no-store');
    return new Response(response.body, { status: response.status, headers });
  },
};
