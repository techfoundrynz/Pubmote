import type { FirmwareVersion } from '../types';

export function otaUrl(path: string): URL {
  return new URL(path, import.meta.env.VITE_API_BASE_URL || 'https://api.pubmote.com');
}

export async function fetchFirmwareReleases(signal?: AbortSignal): Promise<FirmwareVersion[]> {
  const response = await fetch(otaUrl('/ota/v1/releases?format=web'), {
    signal,
    cache: 'no-store',
  });
  if (!response.ok) throw new Error(`Failed to fetch releases: HTTP ${response.status}`);
  return response.json();
}
