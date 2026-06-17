/**
 * CORS Proxy Utility
 * 
 * Provides functions to fetch GitHub URLs through the CORS proxy.
 * In dev mode, tries localhost first with fallback to production.
 * In production mode, uses api.pubmote.com directly.
 */

const DEV_PROXY_URL = 'http://127.0.0.1:8787/cors';
const PROD_PROXY_URL = 'https://api.pubmote.com/cors';

/**
 * Check if running in development mode
 */
function isDevMode(): boolean {
  return window.location.hostname === 'localhost' ||
    window.location.hostname === '127.0.0.1';
}

/**
 * Get the CORS proxy URL based on environment
 */
export function getCorsProxyUrl(targetUrl: string, useLocalProxy = true): string {
  const proxyBaseUrl = (isDevMode() && useLocalProxy) ? DEV_PROXY_URL : PROD_PROXY_URL;
  return `${proxyBaseUrl}?${encodeURIComponent(targetUrl)}`;
}

/**
 * Fetch a URL through the CORS proxy with automatic fallback
 * 
 * In dev mode:
 * 1. Try local proxy (http://127.0.0.1:8787/cors)
 * 2. If fails, fallback to production proxy (https://api.pubmote.com/cors)
 * 
 * In production mode:
 * - Use production proxy directly
 */
export async function fetchWithCorsProxy(
  url: string,
  options?: RequestInit
): Promise<Response> {
  // In production, use prod proxy directly
  if (!isDevMode()) {
    const proxyUrl = getCorsProxyUrl(url, false);
    return fetch(proxyUrl, options);
  }

  // In dev mode, try local proxy first
  try {
    const localProxyUrl = getCorsProxyUrl(url, true);
    const response = await fetch(localProxyUrl, {
      ...options,
      signal: AbortSignal.timeout(3000), // 3 second timeout for local proxy
    });

    if (!response.ok) {
      throw new Error(`Local proxy returned ${response.status}`);
    }

    return response;
  } catch (error) {
    // Fallback to production proxy
    console.warn('Local CORS proxy unavailable, falling back to production:', error);
    const prodProxyUrl = getCorsProxyUrl(url, false);
    return fetch(prodProxyUrl, options);
  }
}

/**
 * Helper to check if local proxy is available
 */
export async function isLocalProxyAvailable(): Promise<boolean> {
  if (!isDevMode()) return false;

  try {
    const response = await fetch(DEV_PROXY_URL, {
      signal: AbortSignal.timeout(1000),
    });
    return response.ok || response.status === 404; // 404 is ok, means proxy is running
  } catch {
    return false;
  }
}
