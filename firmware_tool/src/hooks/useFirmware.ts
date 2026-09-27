import { useEffect, useState } from 'react';
import { FirmwareVersion } from '../types';

export function useFirmware() {
  const [versions, setVersions] = useState<FirmwareVersion[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const controller = new AbortController();
    async function loadFirmware() {
      try {
        const base = import.meta.env.VITE_API_BASE_URL || 'https://api.pubmote.com';
        const response = await fetch(`${base}/ota/v1/releases?format=web`, {
          signal: controller.signal,
          cache: 'no-store',
        });
        if (!response.ok) throw new Error(`Failed to fetch releases: HTTP ${response.status}`);
        const releases: FirmwareVersion[] = await response.json();
        if (!controller.signal.aborted) setVersions(releases);
      } catch (err) {
        if (!controller.signal.aborted)
          setError(err instanceof Error ? err.message : 'Failed to load firmware versions');
      } finally {
        if (!controller.signal.aborted) setLoading(false);
      }
    }
    void loadFirmware();
    return () => controller.abort();
  }, []);

  return { versions, loading, error };
}
