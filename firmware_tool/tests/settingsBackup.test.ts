import { describe, expect, it } from 'vitest';
import {
  createSettingsBackup,
  mergeSettingsBackup,
  migrateBackupValues,
} from '../src/services/settingsBackup';
import { type SettingsMetadata } from '../src/services/settingsProtocol';

const metadata: SettingsMetadata = {
  kind: 'settings',
  version: 1,
  schema: 2,
  warning: '',
  fields: [
    {
      key: 'wifi_password',
      label: 'Password',
      group: 'Wi-Fi',
      description: '',
      readOnly: false,
      type: 'string',
      maxBytes: 64,
      secret: true,
    },
    {
      key: 'brightness',
      label: 'Brightness',
      group: 'Display',
      description: '',
      readOnly: false,
      type: 'range',
      min: 10,
      max: 255,
      color: false,
    },
    {
      key: 'mode',
      label: 'Mode',
      group: 'Display',
      description: '',
      readOnly: false,
      type: 'integer',
      options: [
        { value: 0, label: 'Off' },
        { value: 2, label: 'On' },
      ],
    },
  ],
  values: { wifi_password: '  "secret" \\ 雪  ', brightness: 200, mode: 0 },
};

const backup = (values: Record<string, unknown>) => ({
  format: 'pubmote-settings',
  version: 1,
  values,
});

describe('settings backups', () => {
  it('round trips all saved values, including credentials, without copying field definitions', () => {
    const exported = JSON.parse(createSettingsBackup(metadata, '0.9.5'));
    expect(exported.firmwareVersion).toBe('0.9.5');
    expect(exported.fields).toBeUndefined();
    expect(mergeSettingsBackup(metadata, exported)).toEqual({
      values: metadata.values,
      skipped: [],
      restored: 3,
      newer: false,
    });
  });

  it('merges older backups while preserving settings introduced in current firmware', () => {
    const merged = mergeSettingsBackup(metadata, {
      ...backup({ brightness: 100 }),
      firmwareVersion: '0.1.0',
    });
    expect(merged.values).toEqual({ ...metadata.values, brightness: 100 });
    expect(metadata.values.brightness).toBe(200);
    expect(merged.restored).toBe(1);
  });

  it('skips removed fields and values incompatible with current ranges and choices', () => {
    const merged = mergeSettingsBackup(
      metadata,
      backup({
        removed: 1,
        brightness: 256,
        mode: 1,
        wifi_password: 'restored',
      }),
    );
    expect(merged.values).toEqual({
      ...metadata.values,
      wifi_password: 'restored',
    });
    expect(merged.skipped).toEqual(['removed', 'brightness', 'mode']);
    expect(merged.restored).toBe(1);
  });

  it('preserves falsey values such as an empty password and zero choice', () => {
    const current = { ...metadata, values: { ...metadata.values, mode: 2 } };
    expect(mergeSettingsBackup(current, backup({ wifi_password: '', mode: 0 })).values).toEqual({
      ...metadata.values,
      wifi_password: '',
    });
  });

  it('skips wrong types and strings exceeding current UTF-8 limits', () => {
    const merged = mergeSettingsBackup(
      metadata,
      backup({ brightness: '20', mode: {}, wifi_password: '雪'.repeat(22) }),
    );
    expect(merged.values).toEqual(metadata.values);
    expect(merged.restored).toBe(0);
    expect(merged.skipped).toHaveLength(3);
  });

  it.each([null, [], {}, { ...backup({}), format: 'other' }, { ...backup({}), version: 99 }])(
    'rejects malformed or unsupported backup envelopes: %j',
    (input) => {
      expect(() => mergeSettingsBackup(metadata, input)).toThrow();
    },
  );

  it('does not restore prototype properties', () => {
    const input = JSON.parse(
      '{"format":"pubmote-settings","version":1,"values":{"__proto__":{},"constructor":2,"brightness":100}}',
    );
    const result = mergeSettingsBackup(metadata, input);
    expect(result.values).toEqual({ ...metadata.values, brightness: 100 });
    expect(Object.getPrototypeOf(result.values)).toBe(Object.prototype);
  });

  it('records the settings schema and migrates backups from before it was recorded', () => {
    expect(JSON.parse(createSettingsBackup(metadata)).schema).toBe(2);
    const merged = mergeSettingsBackup(metadata, backup({ brightness: 120 }));
    expect(merged).toMatchObject({ restored: 1, newer: false });
    expect(merged.values.brightness).toBe(120);
    expect(() => migrateBackupValues({}, 1, 3)).toThrow('No migration from settings schema 2');
  });

  it('flags backups from newer firmware but restores the fields that still validate', () => {
    const merged = mergeSettingsBackup(metadata, {
      ...backup({ brightness: 150, added_later: 1 }),
      schema: 3,
    });
    expect(merged).toMatchObject({ newer: true, restored: 1, skipped: ['added_later'] });
  });

  it('restores list settings item by item validated', () => {
    const withBoards: SettingsMetadata = {
      ...metadata,
      fields: [
        ...metadata.fields,
        {
          key: 'boards',
          label: 'Boards',
          group: 'Pairing',
          description: '',
          readOnly: true,
          type: 'list',
          maxItems: 2,
          items: [
            { key: 'mac', label: 'MAC', type: 'string', maxBytes: 17, secret: false },
            {
              key: 'secret',
              label: 'Code',
              type: 'range',
              min: 0,
              max: 99,
              color: false,
              secret: true,
            },
          ],
        },
      ],
      values: { ...metadata.values, boards: [] },
    };
    const boards = [{ mac: 'AA:BB:CC:DD:EE:FF', secret: 7 }];
    const exported = JSON.parse(
      createSettingsBackup({ ...withBoards, values: { ...withBoards.values, boards } }),
    );
    expect(mergeSettingsBackup(withBoards, exported).values.boards).toEqual(boards);
    const invalid = mergeSettingsBackup(
      withBoards,
      backup({ boards: [{ mac: 'AA', secret: 100 }] }),
    );
    expect(invalid).toMatchObject({ restored: 0, skipped: ['boards'] });
    expect(invalid.values.boards).toEqual([]);
  });
});
