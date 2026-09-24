import { describe, expect, test, vi } from 'vitest';
import { requestSettingsJson } from '../src/services/settingsProtocol';
import { ESPService } from '../src/services/espService';

vi.mock('../src/services/stacktraceService', () => ({
  StacktraceService: class {
    isElfLoaded() {
      return false;
    }
  },
}));

async function emit(service: ESPService, line: string) {
  await (service as unknown as { processEspLog(data: string): Promise<void> }).processEspLog(
    line + '\n',
  );
}

// Replies echo the request id, the last word of the most recent write.
function lastId(write: { mock: { calls: unknown[][] } }) {
  const data = write.mock.calls.at(-1)?.[0] as Uint8Array;
  return new TextDecoder().decode(data).trim().split(' ').at(-1);
}

function monitorChunks(service: ESPService, chunks: Uint8Array[]) {
  const rawRead = vi.fn(async () => chunks.shift());
  Object.assign(service, { espLoader: { transport: { rawRead } } });
  service.addSerialMonitor();
  return rawRead;
}

describe('serial JSON frames', () => {
  test('preserves fragmented Unicode and does not mistake values for crash logs', async () => {
    const service = new ESPService();
    const received: string[] = [];
    const onReboot = vi.fn();
    service.onReboot = onReboot;
    service.addLogListener((line) => {
      received.push(line);
      return true;
    });
    const json = JSON.stringify({ value: 'é雪😀\u0080 rst: Backtrace:' });
    const bytes = new TextEncoder().encode(json + '\r\npubconsole>');
    const chunks = Array.from(bytes, (byte) => Uint8Array.of(byte));
    monitorChunks(service, chunks);
    await vi.waitFor(() => expect(received).toEqual([json, 'pubconsole>']));
    expect(onReboot).not.toHaveBeenCalled();
  });

  test('a new monitor discards an incomplete frame from the previous connection', async () => {
    const service = new ESPService();
    const received: string[] = [];
    service.addLogListener((line) => {
      received.push(line);
      return true;
    });
    const firstRead = monitorChunks(service, [new TextEncoder().encode('{"old":')]);
    await vi.waitFor(() => expect(firstRead).toHaveBeenCalledTimes(2));
    monitorChunks(service, [new TextEncoder().encode('{"new":true}\n')]);
    await vi.waitFor(() => expect(received).toEqual(['{"new":true}']));
  });

  test('a failed silent command releases its listener', async () => {
    const service = new ESPService();
    Object.assign(service, {
      espLoader: {
        transport: {
          write: vi.fn().mockRejectedValue(new Error('Disconnected')),
          disconnect: vi.fn(async () => {}),
        },
      },
    });
    await expect(service.executeCommand('version')).rejects.toThrow('Disconnected');
    const listener = vi.fn(() => true);
    service.addLogListener(listener);
    monitorChunks(service, [new TextEncoder().encode('after failure\n')]);
    await vi.waitFor(() => expect(listener).toHaveBeenCalledWith('after failure', 'info'));
  });
});

describe('console write queue', () => {
  test('concurrent commands wait for the writable stream lock and retain order', async () => {
    const service = new ESPService();
    const received: string[] = [];
    let release = () => {};
    const pending = new Promise<void>((resolve) => {
      release = resolve;
    });
    const stream = new WritableStream<Uint8Array>({
      write(data) {
        received.push(new TextDecoder().decode(data));
        if (received.length === 1) return pending;
      },
    });
    const write = vi.fn(async (data: Uint8Array) => {
      const writer = stream.getWriter();
      try {
        await writer.write(data);
      } finally {
        writer.releaseLock();
        await emit(service, 'pubconsole>');
      }
    });
    Object.assign(service, { espLoader: { transport: { write } } });
    const results = Promise.allSettled([
      service.sendCommand('version'),
      service.sendCommand('settings', true),
      service.sendCommand('settings', true),
    ]);
    await vi.waitFor(() => expect(received).toEqual(['version\n']));
    expect(write).toHaveBeenCalledTimes(1);
    expect(stream.locked).toBe(true);
    release();
    expect((await results).map((result) => result.status)).toEqual([
      'fulfilled',
      'fulfilled',
      'fulfilled',
    ]);
    expect(received).toEqual(['version\n', 'settings\n', 'settings\n']);
    expect(stream.locked).toBe(false);
  });

  test('a rejected write invalidates queued requests instead of reusing uncertain framing', async () => {
    const service = new ESPService();
    const write = vi
      .fn()
      .mockRejectedValueOnce(new Error('Write failed'))
      .mockResolvedValue(undefined);
    Object.assign(service, { espLoader: { transport: { write } } });
    const results = await Promise.allSettled([
      service.sendCommand('version'),
      service.sendCommand('settings'),
    ]);
    expect(results[0]).toMatchObject({
      status: 'rejected',
      reason: new Error('Write failed'),
    });
    expect(results[1].status).toBe('rejected');
    expect(write).toHaveBeenCalledTimes(1);
    expect(service.isConnected()).toBe(false);
  });

  test('disconnect cancels queued commands and drains the active write before closing', async () => {
    const service = new ESPService();
    let release = () => {};
    const pending = new Promise<void>((resolve) => {
      release = resolve;
    });
    const write = vi.fn(() => pending);
    const disconnect = vi.fn(async () => {});
    Object.assign(service, { espLoader: { transport: { write, disconnect } } });
    const first = expect(service.sendCommand('version')).rejects.toThrow(
      'Console connection changed',
    );
    const cancelled = expect(service.sendCommand('settings')).rejects.toThrow('Connection changed');
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1));
    const closing = service.disconnect();
    expect(service.isConnected()).toBe(false);
    expect(disconnect).not.toHaveBeenCalled();
    const nextWrite = vi.fn(async () => {
      await emit(service, 'pubconsole>');
    });
    Object.assign(service, { espLoader: { transport: { write: nextWrite } } });
    release();
    await Promise.all([first, cancelled, closing]);
    expect(write).toHaveBeenCalledTimes(1);
    expect(disconnect).toHaveBeenCalledTimes(1);
    await service.sendCommand('settings');
    expect(nextWrite).toHaveBeenCalledTimes(1);
  });
});

describe('console response ownership', () => {
  function connected() {
    const service = new ESPService();
    const write = vi.fn(async (_data: Uint8Array) => {});
    const disconnect = vi.fn(async () => {});
    Object.assign(service, { espLoader: { transport: { write, disconnect } } });
    return { service, write, disconnect };
  }

  test('cancellation drains the old response and prompt before starting its replacement', async () => {
    const { service, write } = connected();
    const controller = new AbortController();
    const first = requestSettingsJson(service, 'settings', 'settings', controller.signal);
    const cancelled = expect(first).rejects.toThrow('cancelled');
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1));
    controller.abort();
    await cancelled;
    const replacement = requestSettingsJson(service, 'settings', 'settings');
    const staleId = lastId(write);
    await emit(service, `{"kind":"settings","value":"stale","id":"${staleId}"}`);
    expect(write).toHaveBeenCalledTimes(1);
    await emit(service, 'pubconsole>');
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(2));
    expect(lastId(write)).not.toBe(staleId);
    await emit(service, `{"kind":"settings","value":"fresh","id":"${lastId(write)}"}`);
    await emit(service, 'pubconsole>');
    expect(await replacement).toEqual({ kind: 'settings', value: 'fresh' });
  });

  test("autocomplete and settings never consume each other's output", async () => {
    const { service, write } = connected();
    const completion = service.executeCommand('complete "s"');
    const settings = requestSettingsJson(service, 'settings', 'settings');
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1));
    await emit(service, 'settings');
    await emit(service, 'save_settings');
    await emit(service, 'pubconsole>');
    expect(await completion).toEqual(['settings', 'save_settings']);
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(2));
    await emit(service, `{"kind":"settings","value":123,"id":"${lastId(write)}"}`);
    await emit(service, 'pubconsole>');
    expect(await settings).toEqual({ kind: 'settings', value: 123 });
  });

  test('a missing prompt resyncs the console before a queued request is sent', async () => {
    vi.useFakeTimers();
    try {
      const { service, write } = connected();
      const first = requestSettingsJson(service, 'settings', 'settings').catch(
        (error) => error.message,
      );
      const next = requestSettingsJson(service, 'settings', 'settings');
      await vi.advanceTimersByTimeAsync(7001);
      expect(await first).toContain('No JSON settings response');
      expect(service.isConnected()).toBe(true);
      expect(write).toHaveBeenCalledTimes(2);
      expect(new TextDecoder().decode(write.mock.calls[1][0])).toBe('\n');
      await emit(service, 'pubconsole>'); // Late prompt of the timed-out command
      await vi.advanceTimersByTimeAsync(100);
      await emit(service, 'pubconsole>'); // Prompt for the newline
      expect(write).toHaveBeenCalledTimes(2);
      await vi.advanceTimersByTimeAsync(250);
      expect(write).toHaveBeenCalledTimes(3);
      await emit(service, `{"kind":"settings","ok":1,"id":"${lastId(write)}"}`);
      await emit(service, 'pubconsole>');
      expect(await next).toEqual({ kind: 'settings', ok: 1 });
    } finally {
      vi.useRealTimers();
    }
  });

  test('a console that stays silent fails queued requests but keeps the connection', async () => {
    vi.useFakeTimers();
    try {
      const { service, write } = connected();
      // A terminal command completes once written; only the queue waits for its prompt.
      await service.sendCommand('reboot');
      const queued = service.executeCommand('version').catch((e) => e.message);
      await vi.advanceTimersByTimeAsync(7001);
      expect(write).toHaveBeenCalledTimes(2);
      await vi.advanceTimersByTimeAsync(3001);
      expect(await queued).toContain('Console busy');
      expect(service.isConnected()).toBe(true);
      const later = service.executeCommand('version');
      await vi.advanceTimersByTimeAsync(0);
      expect(write).toHaveBeenCalledTimes(3);
      await emit(service, 'pubconsole>');
      await vi.advanceTimersByTimeAsync(250);
      expect(write).toHaveBeenCalledTimes(4);
      await emit(service, 'version: 1.0');
      await emit(service, 'pubconsole>');
      expect(await later).toEqual(['version: 1.0']);
    } finally {
      vi.useRealTimers();
    }
  });

  test('a queued cancellation never sends its command', async () => {
    const { service, write } = connected();
    const first = requestSettingsJson(service, 'settings', 'settings');
    const controller = new AbortController();
    const next = requestSettingsJson(service, 'settings', 'settings', controller.signal);
    const cancelled = expect(next).rejects.toThrow('cancelled');
    controller.abort();
    await cancelled;
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1));
    await emit(service, `{"kind":"settings","id":"${lastId(write)}"}`);
    await emit(service, 'pubconsole>');
    await first;
    expect(write).toHaveBeenCalledTimes(1);
  });

  test('manual terminal commands retain their visible output', async () => {
    const { service } = connected();
    const listener = vi.fn(() => true);
    service.addLogListener(listener);
    await service.sendCommand('version');
    await emit(service, 'version: 0.9.5');
    await emit(service, 'pubconsole>');
    expect(listener).toHaveBeenCalledWith('version: 0.9.5', 'info');
  });

  test('a prompt followed by a log line on the same line still ends the command', async () => {
    const { service, write } = connected();
    const listener = vi.fn(() => false);
    service.addLogListener(listener);
    const first = service.executeCommand('coredump_info');
    const next = requestSettingsJson(service, 'settings', 'settings');
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1));
    await emit(service, 'pubconsole> I (3297) SensorLib: Tap was detected');
    expect(await first).toEqual([]);
    await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(2));
    expect(listener).toHaveBeenCalledWith('(3297) SensorLib: Tap was detected', 'info');
    await emit(service, `{"kind":"settings","id":"${lastId(write)}"}`);
    await emit(service, 'pubconsole>');
    expect(await next).toEqual({ kind: 'settings' });
  });
});
