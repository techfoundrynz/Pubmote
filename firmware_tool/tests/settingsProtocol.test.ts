import assert from 'node:assert/strict';
import { test, vi } from 'vitest';
import { execFileSync } from 'node:child_process';
import {
  parseSettings,
  settingsValuesSchema,
  settingsSaveCommand,
  settingsSaveCommands,
  requestSettingsJson,
  type SettingsMetadata,
  type SettingsTransport,
} from '../src/services/settingsProtocol';

const payload = (): SettingsMetadata => ({
  kind: 'settings',
  version: 1,
  schema: 2,
  warning: '',
  fields: [
    {
      key: 'new_text',
      label: 'New text',
      group: 'Example',
      description: '',
      readOnly: false,
      type: 'string',
      maxBytes: 4,
      secret: false,
    },
    {
      key: 'new_choice',
      label: 'New choice',
      group: 'Example',
      description: '',
      readOnly: false,
      type: 'integer',
      options: [
        { value: -1, label: 'Off' },
        { value: 17, label: 'On' },
      ],
    },
  ],
  values: { new_text: '', new_choice: -1 },
});

test('firmware metadata defines new fields, choices, and UTF-8 limits without tool changes', () => {
  const metadata = parseSettings(payload());
  const schema = settingsValuesSchema(metadata);
  assert.deepEqual(schema.parse({ new_text: 'éé', new_choice: 17 }), {
    new_text: 'éé',
    new_choice: 17,
  });
  for (const values of [
    { new_text: 'ééé', new_choice: 17 },
    { new_text: '\0', new_choice: 17 },
    { new_text: '', new_choice: 18 },
    { new_text: '', new_choice: '17' },
    { new_text: '', new_choice: 17.5 },
    { new_text: '', new_choice: 17, unknown: true },
    { new_choice: 17 },
  ])
    assert.equal(schema.safeParse(values).success, false);
});

test('rejects incompatible or malformed firmware payloads', () => {
  assert.throws(() => parseSettings({ ...payload(), version: 2 }));
  assert.throws(() => parseSettings({ ...payload(), values: {} }));
  const duplicate = payload();
  duplicate.fields.push(duplicate.fields[0]);
  assert.throws(() => parseSettings(duplicate));
  assert.throws(() =>
    parseSettings({
      ...payload(),
      fields: [{ ...payload().fields[0], type: 'unknown' }],
    }),
  );
});

// Undo esp_console_split_argv's escaping; the C harness covers the real splitter.
test('JSON command preserves quotes, slashes, whitespace and Unicode', () => {
  const values = { text: '  "quoted" \\ slash\n\t é雪 😀  ' };
  const command = settingsSaveCommand(values);
  assert.equal(command.includes('\n'), false);
  const encoded = command.slice('save_settings "'.length, -1);
  const json = encoded.replace(/\\([\\" ])/g, '$1');
  assert.deepEqual(JSON.parse(json), values);
  assert.throws(() => settingsSaveCommand({ text: 'x'.repeat(2048) }));
});

const listMetadata = (): SettingsMetadata => ({
  ...payload(),
  fields: [
    ...payload().fields,
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
        { key: 'code', label: 'Code', type: 'range', min: 0, max: 9, color: false, secret: true },
      ],
    },
  ],
  values: { ...payload().values, boards: [{ mac: 'AA:BB:CC:DD:EE:FF', code: 1 }] },
});

test('list settings validate item count, keys and item ranges', () => {
  const schema = settingsValuesSchema(parseSettings(listMetadata()));
  const base = listMetadata().values;
  const board = { mac: 'AA:BB:CC:DD:EE:FF', code: 1 };
  assert.equal(schema.safeParse({ ...base, boards: [board, board] }).success, true);
  for (const boards of [
    [board, board, board],
    [{ ...board, code: 10 }],
    [{ mac: board.mac }],
    [{ ...board, extra: 1 }],
    [{ ...board, mac: 'x'.repeat(18) }],
    board,
  ])
    assert.equal(schema.safeParse({ ...base, boards }).success, false);
});

test('fields default to editable when firmware omits readOnly', () => {
  const raw = JSON.parse(JSON.stringify(payload()));
  for (const field of raw.fields) delete field.readOnly;
  assert.equal(parseSettings(raw).fields[0].readOnly, false);
});

test('large saves split between fields in metadata order and stay under the command limit', () => {
  const metadata = listMetadata();
  const patch = { new_text: 'ab', new_choice: 17, boards: [{ mac: 'AA:BB:CC:DD:EE:FF', code: 1 }] };
  assert.deepEqual(settingsSaveCommands(patch, metadata), [settingsSaveCommand(patch)]);
  const big: SettingsMetadata = {
    ...metadata,
    fields: Array.from({ length: 40 }, (_, i) => ({
      ...metadata.fields[0],
      key: `text_${i}`,
      maxBytes: 64,
    })),
    values: {},
  };
  const bigPatch = Object.fromEntries(big.fields.map((field) => [field.key, '"'.repeat(60)]));
  const commands = settingsSaveCommands(bigPatch, big);
  assert.ok(commands.length > 1);
  const bytes = (command: string) => new TextEncoder().encode(command).length;
  for (const command of commands) assert.ok(bytes(command) < 2048 - 32);
  const keys = commands.flatMap((command) => {
    const json = command.slice('save_settings "'.length, -1).replace(/\\([\\" ])/g, '$1');
    return Object.keys(JSON.parse(json));
  });
  assert.deepEqual(
    keys,
    big.fields.map((field) => field.key),
  );
});

type LogListener = Parameters<SettingsTransport['addLogListener']>[0];

function transport(send: SettingsTransport['sendCommand'] = async () => {}) {
  const listeners = new Set<LogListener>();
  const sent: string[] = [];
  const emit = (line: string) => {
    for (const listener of [...listeners]) listener(line, 'info');
  };
  return {
    listeners,
    sent,
    addLogListener: (listener: LogListener) => listeners.add(listener),
    removeLogListener: (listener: LogListener) => listeners.delete(listener),
    sendCommand: (command: string, silent?: boolean) => {
      sent.push(command);
      return send(command, silent);
    },
    emit,
    // Answer the most recent request the way firmware does, echoing its id.
    reply: (body: object) => emit(JSON.stringify({ ...body, id: sent.at(-1)?.split(' ').at(-1) })),
  };
}

test('waits for typed JSON, ignores prompts/logs, and cleans up after acknowledgement', async () => {
  const serial = transport();
  const request = requestSettingsJson(serial, 'settings', 'settings');
  serial.emit('pubconsole>');
  serial.emit('wifi_ssid: old firmware');
  serial.emit('{bad json');
  assert.equal(serial.listeners.size, 1);
  serial.reply(payload());
  assert.deepEqual(await request, payload());
  assert.equal(serial.listeners.size, 0);
});

test('device rejection, send failure and cancellation reject and remove listeners', async () => {
  const serial = transport();
  const request = requestSettingsJson(serial, 'save_settings', 'settings_result');
  serial.reply({
    kind: 'settings_result',
    version: 1,
    ok: false,
    error: 'pins conflict',
  });
  await assert.rejects(request, /pins conflict/);
  assert.equal(serial.listeners.size, 0);
  const failing = transport(async () => {
    throw new Error('disconnected');
  });
  await assert.rejects(requestSettingsJson(failing, 'settings', 'settings'), /disconnected/);
  assert.equal(failing.listeners.size, 0);
  const controller = new AbortController();
  const cancelled = requestSettingsJson(serial, 'settings', 'settings', controller.signal);
  controller.abort();
  await assert.rejects(cancelled, /cancelled/);
  assert.equal(serial.listeners.size, 0);
});

test.skipIf(!process.env.SETTINGS_CONSOLE_TEST_BIN)(
  'actual firmware metadata and console parser round trip the tool JSON',
  () => {
    const binary = process.env.SETTINGS_CONSOLE_TEST_BIN;
    assert.ok(binary);
    const metadata = parseSettings(
      JSON.parse(execFileSync(binary, ['metadata'], { encoding: 'utf8' })),
    );
    assert.equal(metadata.fields.length, 38);
    assert.equal(metadata.schema, 2);
    const patch = {
      wifi_ssid: '  "é雪" \\ 😀  ',
      wifi_password: 'space\tquote"slash\\newline\n',
      js_x_gpio: 1,
      bl_level: 255,
      theme_color: 0xff8844,
      screen_rotation: 3,
      auto_off_time: 4,
      startup_sound: 2,
      led_mode: 2,
      stick_x_min: 321,
      stick_expo: 250,
      imu_offset_y: -42,
      paired_boards: [
        { mac: 'AA:BB:CC:DD:EE:0F', transport: 1, channel: 6, secret: 4294967295, vehicle: 2 },
      ],
      default_board: 0,
    };
    settingsValuesSchema(metadata).parse({ ...metadata.values, ...patch });
    const lines = execFileSync(binary, ['command'], {
      input: settingsSaveCommand(patch) + ' t1\n',
      encoding: 'utf8',
    })
      .trim()
      .split(/\r?\n/)
      .map((line) => JSON.parse(line));
    assert.equal(lines[0].ok, true);
    assert.equal(lines[0].id, 't1');
    assert.equal(lines[1].id, 't1');
    const applied = parseSettings(lines[1]);
    for (const [key, value] of Object.entries(patch)) assert.deepEqual(applied.values[key], value);
  },
);

test('times out without an acknowledgement and removes the listener', async () => {
  vi.useFakeTimers();
  try {
    const serial = transport();
    const result = requestSettingsJson(serial, 'settings', 'settings');
    const rejected = assert.rejects(result, /No JSON settings response/);
    await vi.advanceTimersByTimeAsync(5000);
    await rejected;
    assert.equal(serial.listeners.size, 0);
  } finally {
    vi.useRealTimers();
  }
});

test.each([
  { kind: 'settings_result', version: 2, ok: true },
  { kind: 'settings_result', version: 1, ok: 'true' },
  { kind: 'settings_result', version: 1, ok: false, error: 123 },
])('rejects malformed acknowledgement %j', async (reply) => {
  const serial = transport();
  const request = requestSettingsJson(serial, 'save_settings', 'settings_result');
  serial.reply(reply);
  await assert.rejects(request, /Invalid settings acknowledgement/);
  assert.equal(serial.listeners.size, 0);
});

test('an already-cancelled request sends no command', async () => {
  const send = vi.fn(async () => {});
  const serial = transport(send);
  const controller = new AbortController();
  controller.abort();
  await assert.rejects(
    requestSettingsJson(serial, 'settings', 'settings', controller.signal),
    /cancelled/,
  );
  assert.equal(send.mock.calls.length, 0);
  assert.equal(serial.listeners.size, 0);
});

test('synchronous send failure removes listeners and timers', async () => {
  vi.useFakeTimers();
  try {
    const serial = transport(() => {
      throw new Error('port closed');
    });
    await assert.rejects(requestSettingsJson(serial, 'settings', 'settings'), /port closed/);
    assert.equal(serial.listeners.size, 0);
    assert.equal(vi.getTimerCount(), 0);
  } finally {
    vi.useRealTimers();
  }
});

test('an unrelated successful acknowledgement does not complete a metadata request', async () => {
  const serial = transport();
  const request = requestSettingsJson(serial, 'settings', 'settings');
  serial.reply({ kind: 'settings_result', version: 1, ok: true });
  assert.equal(serial.listeners.size, 1);
  serial.reply(payload());
  assert.deepEqual(await request, payload());
});

test('replies to other requests are ignored, even failures', async () => {
  const serial = transport();
  const request = requestSettingsJson(serial, 'save_settings', 'settings_result');
  const [command, id] = serial.sent[0].split(' ');
  assert.equal(command, 'save_settings');
  assert.match(id, /^[a-z0-9]{1,32}$/);
  const failure = { kind: 'settings_result', version: 1, ok: false };
  serial.emit(JSON.stringify({ ...failure, error: 'stale', id: 'old1' }));
  serial.emit(JSON.stringify({ ...failure, error: 'untagged' }));
  assert.equal(serial.listeners.size, 1);
  serial.reply({ kind: 'settings_result', version: 1, ok: true });
  assert.deepEqual(await request, {
    kind: 'settings_result',
    version: 1,
    ok: true,
  });
  const controller = new AbortController();
  const next = requestSettingsJson(serial, 'settings', 'settings', controller.signal);
  assert.notEqual(serial.sent[1].split(' ')[1], id);
  controller.abort();
  await assert.rejects(next, /cancelled/);
});

test.each(['constructor', 'prototype', '__proto__'])('rejects reserved field key %s', (key) => {
  const metadata = payload();
  metadata.fields[0].key = key;
  assert.throws(() => parseSettings(metadata));
});

test('numeric metadata validates brightness and colour boundaries', () => {
  const metadata = payload();
  metadata.fields.push({
    key: 'brightness',
    label: 'Brightness',
    group: 'Display',
    description: '',
    readOnly: false,
    type: 'range',
    min: 10,
    max: 255,
    color: false,
  });
  metadata.values.brightness = 200;
  const schema = settingsValuesSchema(parseSettings(metadata));
  for (const value of [10, 255])
    assert.equal(schema.safeParse({ ...metadata.values, brightness: value }).success, true);
  for (const value of [9, 256, 10.5, '', '20', NaN, Infinity])
    assert.equal(schema.safeParse({ ...metadata.values, brightness: value }).success, false);
  assert.equal(metadata.fields[2].type, 'range');
  if (metadata.fields[2].type !== 'range') throw new Error('Expected range field');
  const range = metadata.fields[2];
  metadata.fields[2] = { ...range, min: 0, max: 0xffffff, color: true };
  const colorSchema = settingsValuesSchema(parseSettings(metadata));
  for (const value of [0, 0xffffff])
    assert.equal(colorSchema.safeParse({ ...metadata.values, brightness: value }).success, true);
  for (const value of [-1, 0x1000000, 1.5])
    assert.equal(colorSchema.safeParse({ ...metadata.values, brightness: value }).success, false);
  metadata.fields[2] = { ...range, min: 255, max: 10 };
  assert.throws(() => parseSettings(metadata));
});
