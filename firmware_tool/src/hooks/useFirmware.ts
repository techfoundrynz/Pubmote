import { useEffect, useState } from 'react';
import { FirmwareVersion } from '../types';
import { fetchFirmwareReleases } from '../utils/ota';

export function useFirmware() {
  const [versions, setVersions] = useState<FirmwareVersion[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const controller = new AbortController();
    async function loadFirmware() {
      try {
        const releases = await fetchFirmwareReleases(controller.signal);
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
