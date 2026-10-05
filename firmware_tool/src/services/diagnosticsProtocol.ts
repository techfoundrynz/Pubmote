import { z } from 'zod';

const counter = z.number().int().nonnegative();
const diagnosticsSchema = z.object({
  kind: z.literal('diagnostics'),
  version: z.literal(1),
  ok: z.literal(true),
  capture_available: z.boolean(),
  build_id: z.string(),
  hardware: z.string(),
  uptime_ms: z.number().nonnegative(),
  internal_free: counter,
  internal_min: counter,
  internal_largest: counter,
  psram_free: counter,
  overwritten: counter,
  truncated: counter,
  dropped: counter,
  log: z.string().max(16 * 1024),
});

export type DiagnosticsReport = z.infer<typeof diagnosticsSchema>;

export function parseDiagnostics(lines: string[]): DiagnosticsReport {
  for (const line of lines) {
    let value: unknown;
    try {
      value = JSON.parse(line);
    } catch {
      continue;
    }
    if (!value || typeof value !== 'object' || !('kind' in value) || value.kind !== 'diagnostics')
      continue;
    if ('ok' in value && value.ok === false) {
      throw new Error(
        'error' in value && typeof value.error === 'string'
          ? value.error
          : 'Diagnostic capture failed',
      );
    }
    const parsed = diagnosticsSchema.safeParse(value);
    if (!parsed.success) throw new Error('Device returned an invalid diagnostic report');
    return parsed.data;
  }
  throw new Error('No diagnostic report received. Install firmware that supports diagnostics.');
}
