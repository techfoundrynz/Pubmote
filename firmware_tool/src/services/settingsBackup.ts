import { z } from 'zod';
import { settingsValuesSchema, type SettingsMetadata } from './settingsProtocol';

const backupSchema = z.object({
  format: z.literal('pubmote-settings'),
  version: z.literal(1),
  // Backups from before the schema was recorded are schema 1.
  schema: z.number().int().positive().default(1),
  values: z.record(z.string(), z.unknown()),
});

type BackupValues = Record<string, unknown>;

// migrations[n] upgrades values from settings schema n to n + 1.
const migrations: Record<number, (values: BackupValues) => BackupValues> = {
  1: (values) => values, // Schema 2 only added fields.
};

export function migrateBackupValues(values: BackupValues, from: number, to: number) {
  let migrated = values;
  for (let schema = from; schema < to; ++schema) {
    const step = migrations[schema];
    if (!step) throw new Error(`No migration from settings schema ${schema}`);
    migrated = step(migrated);
  }
  return migrated;
}

export function createSettingsBackup(metadata: SettingsMetadata, firmwareVersion?: string) {
  return (
    JSON.stringify(
      {
        format: 'pubmote-settings',
        version: 1,
        schema: metadata.schema,
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
  const newer = backup.schema > metadata.schema;
  const migrated = newer
    ? backup.values
    : migrateBackupValues(backup.values, backup.schema, metadata.schema);
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
