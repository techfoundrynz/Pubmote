import { z } from 'zod';
import { settingsValuesSchema, type SettingsMetadata } from './settingsProtocol';

const backupSchema = z.object({
  format: z.literal('pubmote-settings'),
  version: z.number().int().positive(),
  values: z.record(z.string(), z.unknown()),
});

type BackupValues = Record<string, unknown>;

// migrations[n] upgrades values from settings version n to n + 1.
const migrations: Record<number, (values: BackupValues) => BackupValues> = {};

export function migrateBackupValues(values: BackupValues, from: number, to: number) {
  let migrated = values;
  for (let version = from; version < to; ++version) {
    const step = migrations[version];
    if (!step) throw new Error(`No migration from settings version ${version}`);
    migrated = step(migrated);
  }
  return migrated;
}

export function createSettingsBackup(metadata: SettingsMetadata, firmwareVersion?: string) {
  return (
    JSON.stringify(
      {
        format: 'pubmote-settings',
        version: metadata.version,
        firmwareVersion,
        values: settingsValuesSchema(metadata).parse(metadata.values),
      },
      null,
      2,
    ) + '\n'
  );
}

export function mergeSettingsBackup(metadata: SettingsMetadata, input: unknown) {
  const backup = backupSchema.parse(input);
  const schema = settingsValuesSchema(metadata);
  const values = { ...metadata.values };
  const skipped: string[] = [];
  let restored = 0;
  const newer = backup.version > metadata.version;
  const migrated = newer
    ? backup.values
    : migrateBackupValues(backup.values, backup.version, metadata.version);
  for (const [key, value] of Object.entries(migrated)) {
    if (!Object.prototype.hasOwnProperty.call(schema.shape, key)) {
      skipped.push(key);
      continue;
    }
    const result = schema.shape[key].safeParse(value);
    if (!result.success) {
      skipped.push(key);
      continue;
    }
    values[key] = result.data;
    ++restored;
  }
  return { values, skipped, restored, newer };
}
