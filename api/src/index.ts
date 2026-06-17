/**
 * CORS Proxy for GitHub Files
 * 
 * This Cloudflare Worker provides a CORS proxy specifically for fetching files
 * from GitHub.com and related domains. It handles preflight requests and adds
 * appropriate CORS headers to enable cross-origin requests.
 * 
 * Usage: https://your-worker.workers.dev/cors?https://github.com/user/repo/file
 */

// Whitelist patterns for allowed target URLs (GitHub domains only)
const ALLOWED_URL_PATTERNS = [
  /^https?:\/\/(www\.)?github\.com\/.*/,
  /^https?:\/\/raw\.githubusercontent\.com\/.*/,
  /^https?:\/\/gist\.github\.com\/.*/,
  /^https?:\/\/api\.github\.com\/.*/,
];

// Whitelist patterns for allowed origins (localhost:5173 and pubmote.com only)
const ALLOWED_ORIGIN_PATTERNS = [
  /^http:\/\/localhost:5173$/,
  /^https?:\/\/(.*\.)?pubmote\.com$/,
  /^https?:\/\/(.*\.)?techfoundrynz\.github\.io$/,
];

/**
 * Check if a URL matches any pattern in the whitelist
 */
function isUrlAllowed(url: string, patterns: RegExp[]): boolean {
  return patterns.some(pattern => pattern.test(url));
}

/**
 * Set up CORS headers for the response
 */
function setupCORSHeaders(headers: Headers, request: Request, isPreflight: boolean): Headers {
  const origin = request.headers.get("Origin");

  if (origin) {
    headers.set("Access-Control-Allow-Origin", origin);
  } else {
    headers.set("Access-Control-Allow-Origin", "*");
  }

  if (isPreflight) {
    const requestMethod = request.headers.get("Access-Control-Request-Method");
    const requestHeaders = request.headers.get("Access-Control-Request-Headers");

    if (requestMethod) {
      headers.set("Access-Control-Allow-Methods", requestMethod);
    }

    if (requestHeaders) {
      headers.set("Access-Control-Allow-Headers", requestHeaders);
    }

    headers.set("Access-Control-Max-Age", "86400"); // 24 hours
  }

  return headers;
}

/**
 * Create an informational response when accessed without parameters
 */
function createInfoResponse(request: Request): Response {
  const url = new URL(request.url);
  const origin = request.headers.get("Origin");
  const ip = request.headers.get("CF-Connecting-IP");

  const headers = new Headers();
  setupCORSHeaders(headers, request, false);
  headers.set("Content-Type", "text/plain");

  const info = [
    "PUBMOTE CORS PROXY FOR GITHUB",
    "",
    "Usage:",
    `${url.origin}/cors?<github-url>`,
    "",
    "Example:",
    `${url.origin}/cors?https://raw.githubusercontent.com/user/repo/main/file.txt`,
    "",
    "Allowed domains:",
    "- github.com",
    "- raw.githubusercontent.com",
    "- gist.github.com",
    "- api.github.com",
    "",
    origin ? `Origin: ${origin}` : "",
    ip ? `IP: ${ip}` : "",
  ].filter(Boolean).join("\n");

  return new Response(info, {
    status: 200,
    headers,
  });
}

/**
 * Create a forbidden response
 */
function createForbiddenResponse(reason: string): Response {
  return new Response(
    JSON.stringify({
      error: "Forbidden",
      reason,
      message: "This CORS proxy only allows requests to GitHub domains.",
    }),
    {
      status: 403,
      headers: {
        "Content-Type": "application/json",
      },
    }
  );
}

/**
 * Main fetch handler
 */
export default {
  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const isPreflight = request.method === "OPTIONS";

    // Handle /cors path
    if (url.pathname === "/cors") {
      // Get the target URL from the query string
      const targetUrl = decodeURIComponent(url.search.substring(1));

      // If no target URL, return info page
      if (!targetUrl || targetUrl === "") {
        return createInfoResponse(request);
      }

      // Check if the target URL is allowed
      if (!isUrlAllowed(targetUrl, ALLOWED_URL_PATTERNS)) {
        return createForbiddenResponse("Target URL is not in the allowed list (GitHub domains only)");
      }

      // Check if the origin is allowed
      const origin = request.headers.get("Origin");
      if (origin && !isUrlAllowed(origin, ALLOWED_ORIGIN_PATTERNS)) {
        return createForbiddenResponse("Origin is not allowed");
      }

      // Handle preflight request
      if (isPreflight) {
        const headers = new Headers();
        setupCORSHeaders(headers, request, true);
        return new Response(null, {
          status: 204,
          headers,
        });
      }

      // Parse custom headers if provided
      let customHeaders: Record<string, string> = {};
      const customHeadersStr = request.headers.get("x-cors-headers");
      if (customHeadersStr) {
        try {
          customHeaders = JSON.parse(customHeadersStr);
        } catch (e) {
          // Ignore invalid JSON
        }
      }

      // Build headers for the proxied request
      const proxyHeaders: Record<string, string> = {};

      // Copy relevant headers from the original request
      for (const [key, value] of request.headers.entries()) {
        // Skip headers that shouldn't be forwarded
        if (
          !key.startsWith("cf-") &&
          !key.startsWith("x-forwarded-") &&
          key !== "origin" &&
          key !== "referer" &&
          key !== "x-cors-headers"
        ) {
          proxyHeaders[key] = value;
        }
      }

      // Apply custom headers
      Object.assign(proxyHeaders, customHeaders);

      // Make the proxied request
      try {
        const proxyRequest = new Request(targetUrl, {
          method: request.method,
          headers: proxyHeaders,
          body: request.method !== "GET" && request.method !== "HEAD" ? request.body : undefined,
          redirect: "follow",
        });

        const response = await fetch(proxyRequest);

        // Build response headers
        const responseHeaders = new Headers(response.headers);
        setupCORSHeaders(responseHeaders, request, false);

        // Expose all response headers to the client
        const exposedHeaders: string[] = [];
        const allResponseHeaders: Record<string, string> = {};

        for (const [key, value] of response.headers.entries()) {
          exposedHeaders.push(key);
          allResponseHeaders[key] = value;
        }

        exposedHeaders.push("cors-received-headers");
        responseHeaders.set("Access-Control-Expose-Headers", exposedHeaders.join(","));
        responseHeaders.set("cors-received-headers", JSON.stringify(allResponseHeaders));

        return new Response(response.body, {
          status: response.status,
          statusText: response.statusText,
          headers: responseHeaders,
        });
      } catch (error) {
        return new Response(
          JSON.stringify({
            error: "Proxy Error",
            message: error instanceof Error ? error.message : "Unknown error occurred",
          }),
          {
            status: 500,
            headers: {
              "Content-Type": "application/json",
            },
          }
        );
      }
    }

    // For any other path, return 404
    return new Response("Not Found", { status: 404 });
  },
};
