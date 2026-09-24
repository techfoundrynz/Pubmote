import { z } from 'zod';

const settingKey = z
  .string()
  .regex(/^[a-z][a-z0-9_]*$/)
  .refine((key) => !['constructor', 'prototype', '__proto__'].includes(key));
const optionsSchema = z.array(z.object({ value: z.number().int(), label: z.string() })).nonempty();
const validRange = (field: { min: number; max: number; color: boolean }) =>
  field.min <= field.max && (!field.color || (field.min === 0 && field.max === 0xffffff));

const itemBase = z.object({
  key: settingKey,
  label: z.string(),
  secret: z.boolean().default(false),
});
const listItemSchema = z.discriminatedUnion('type', [
  itemBase.extend({ type: z.literal('string'), maxBytes: z.number().int().positive() }),
  itemBase
    .extend({
      type: z.literal('range'),
      min: z.number().int().safe(),
      max: z.number().int().safe(),
      color: z.boolean(),
    })
    .refine(validRange, 'Invalid numeric range'),
  itemBase.extend({ type: z.literal('integer'), options: optionsSchema }),
]);

const fieldBase = z.object({
  key: settingKey,
  label: z.string(),
  group: z.string(),
  description: z.string(),
  readOnly: z.boolean().default(false),
});
const fieldSchema = z.discriminatedUnion('type', [
  fieldBase.extend({
    type: z.literal('string'),
    maxBytes: z.number().int().positive(),
    secret: z.boolean(),
  }),
  fieldBase
    .extend({
      type: z.literal('range'),
      min: z.number().int().safe(),
      max: z.number().int().safe(),
      color: z.boolean(),
    })
    .refine(validRange, 'Invalid numeric range'),
  fieldBase.extend({ type: z.literal('integer'), options: optionsSchema }),
  fieldBase.extend({
    type: z.literal('list'),
    maxItems: z.number().int().positive(),
    items: z
      .array(listItemSchema)
      .nonempty()
      .refine(
        (items) => new Set(items.map((item) => item.key)).size === items.length,
        'Duplicate list item metadata',
      ),
  }),
]);

const scalarValue = z.union([z.string(), z.number()]);
const settingValue = z.union([scalarValue, z.array(z.record(z.string(), scalarValue))]);

// Bumped when the message format or any setting's meaning changes.
export const SETTINGS_VERSION = 1;

export const settingsMetadataSchema = z.object({
  kind: z.literal('settings'),
  version: z.literal(SETTINGS_VERSION),
  fields: z
    .array(fieldSchema)
    .nonempty()
    .refine(
      (fields) => new Set(fields.map((field) => field.key)).size === fields.length,
      'Duplicate settings metadata',
    ),
  values: z.record(z.string(), settingValue),
  warning: z.string(),
});
export const settingsResultSchema = z.object({
  kind: z.literal('settings_result'),
  version: z.literal(SETTINGS_VERSION),
  ok: z.boolean(),
  error: z.string().optional(),
});
export type SettingsMetadata = z.infer<typeof settingsMetadataSchema>;
export type SettingsField = SettingsMetadata['fields'][number];
export type ListItemField = z.infer<typeof listItemSchema>;
export type ListValue = Array<Record<string, string | number>>;
export type SettingValue = string | number | ListValue;
export type SettingsValues = Record<string, SettingValue>;

function scalarSchema(field: SettingsField | ListItemField): z.ZodType<string | number> {
  switch (field.type) {
    case 'string':
      return z
        .string()
        .refine(
          (value) =>
            !value.includes('\0') && new TextEncoder().encode(value).length <= field.maxBytes,
          `${field.label} must be at most ${field.maxBytes} UTF-8 bytes and contain no NUL`,
        );
    case 'range':
      return z.number().int().min(field.min).max(field.max);
    case 'integer':
      return z
        .number()
        .int()
        .refine(
          (value) => field.options.some((option) => option.value === value),
          `Choose an allowed value for ${field.label}`,
        );
    case 'list':
      throw new Error('A list is not a scalar setting');
  }
}

export function settingsValuesSchema(metadata: SettingsMetadata) {
  const shape: Record<string, z.ZodType<SettingValue>> = Object.create(null);
  for (const field of metadata.fields) {
    shape[field.key] =
      field.type === 'list'
        ? z
            .array(
              z.strictObject(
                Object.fromEntries(field.items.map((item) => [item.key, scalarSchema(item)])),
              ),
            )
            .max(field.maxItems, `${field.label} holds at most ${field.maxItems} entries`)
        : scalarSchema(field);
  }
  return z.strictObject(shape);
}

export function parseSettings(payload: unknown): SettingsMetadata {
  const metadata = settingsMetadataSchema.parse(payload);
  settingsValuesSchema(metadata).parse(metadata.values);
  return metadata;
}

export const sameSettingValue = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

// Leaves room for the appended request id.
const MAX_COMMAND_BYTES = 2048 - 32;
const commandBytes = (command: string) => new TextEncoder().encode(command).length;

// esp_console_split_argv consumes one layer of backslash/quote escaping.
export function settingsSaveCommand(patch: SettingsValues): string {
  const json = JSON.stringify(patch);
  const command = `save_settings "${json.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
  if (commandBytes(command) >= MAX_COMMAND_BYTES) {
    throw new Error('Settings payload exceeds the console command limit');
  }
  return command;
}

// Split in metadata order so pins save before calibration.
export function settingsSaveCommands(patch: SettingsValues, metadata: SettingsMetadata): string[] {
  const commands: string[] = [];
  let chunk: SettingsValues = {};
  for (const field of metadata.fields) {
    if (!Object.prototype.hasOwnProperty.call(patch, field.key)) continue;
    const next = { ...chunk, [field.key]: patch[field.key] };
    const json = JSON.stringify(next);
    const escaped = `save_settings "${json.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
    if (Object.keys(chunk).length && commandBytes(escaped) >= MAX_COMMAND_BYTES) {
      commands.push(settingsSaveCommand(chunk));
      chunk = { [field.key]: patch[field.key] };
    } else {
      chunk = next;
    }
  }
  if (Object.keys(chunk).length) commands.push(settingsSaveCommand(chunk));
  return commands;
}

type LogListener = (line: string, type: 'info' | 'error' | 'success') => boolean;
export interface SettingsTransport {
  withConsoleTransaction?<T>(
    operation: (transport: SettingsTransport) => Promise<T>,
    signal?: AbortSignal,
  ): Promise<T>;
  addLogListener(listener: LogListener): void;
  removeLogListener(listener: LogListener): void;
  sendCommand(command: string, silent?: boolean): Promise<void>;
}

// The session prefix keeps replies to a previous page load from matching.
const requestPrefix = Math.random().toString(36).slice(2, 8);
let requestCount = 0;

export function requestSettingsJson(
  transport: SettingsTransport,
  command: string,
  kind: 'settings' | 'settings_result',
  signal?: AbortSignal,
): Promise<unknown> {
  if (transport.withConsoleTransaction) {
    return transport.withConsoleTransaction(
      (exclusive) => requestSettingsJson(exclusive, command, kind, signal),
      signal,
    );
  }
  const id = `${requestPrefix}${++requestCount}`;
  return new Promise((resolve, reject) => {
    let settled = false;
    const finish = (error?: Error, payload?: unknown) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      transport.removeLogListener(listener);
      signal?.removeEventListener('abort', abort);
      if (error) reject(error);
      else resolve(payload);
    };
    const abort = () => finish(new Error('Settings request cancelled'));
    const listener: LogListener = (line) => {
      if (!line.startsWith('{')) return false;
      let payload: unknown;
      try {
        payload = JSON.parse(line);
      } catch {
        return false;
      }
      if (!payload || typeof payload !== 'object' || !('kind' in payload)) return false;
      if (!('id' in payload) || payload.id !== id) return false;
      const reply: { kind?: unknown; id?: unknown } = { ...payload };
      delete reply.id;
      if (reply.kind === 'settings_result') {
        const result = settingsResultSchema.safeParse(reply);
        if (!result.success) {
          finish(new Error('Invalid settings acknowledgement from device'));
        } else if (!result.data.ok) {
          finish(new Error(result.data.error || 'Device rejected settings'));
        } else if (kind === 'settings_result') {
          finish(undefined, result.data);
        } else {
          return false;
        }
      } else if (reply.kind === kind) {
        finish(undefined, reply);
      } else return false;
      return true;
    };
    const timer = setTimeout(
      () =>
        finish(
          new Error(
            'No JSON settings response. Check the connection and update firmware if needed.',
          ),
        ),
      5000,
    );
    transport.addLogListener(listener);
    signal?.addEventListener('abort', abort, { once: true });
    if (signal?.aborted) {
      abort();
      return;
    }
    // Avoid logging commands containing Wi-Fi credentials.
    try {
      transport
        .sendCommand(`${command} ${id}`, true)
        .catch((error: unknown) =>
          finish(error instanceof Error ? error : new Error('Unable to send settings command')),
        );
    } catch (error) {
      finish(error instanceof Error ? error : new Error('Unable to send settings command'));
    }
  });
}
