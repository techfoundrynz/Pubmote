/* oxlint-disable no-control-regex */
import { ESPLoader, Transport, LoaderOptions } from 'esptool-js';
import type { SettingsTransport } from './settingsProtocol';
import { delay } from '../utils/delay';
import { LogEntry, TerminalService } from './terminal';
import { FirmwareFiles } from '../types';
import { StacktraceService } from './stacktraceService';

const LogTypePrefixMap: Record<LogEntry['type'], Array<`${string} `>> = {
  info: ['I '],
  error: ['E '],
  success: ['I ', 'W '],
};

const getLogLevel = (data: string): LogEntry['type'] => {
  for (const [type, prefixes] of Object.entries(LogTypePrefixMap)) {
    if (prefixes.some((prefix) => data.startsWith(prefix))) {
      return type as LogEntry['type'];
    }
  }
  return 'info';
};

const removeLogLevelPrefix = (data: string): string => {
  for (const prefixes of Object.values(LogTypePrefixMap)) {
    for (const prefix of prefixes) {
      if (data.startsWith(prefix)) {
        return data.slice(prefix.length);
      }
    }
  }
  return data;
};

const removeAnsiEscapeCodes = (data: string): string => {
  return (
    data
      // Remove ANSI escape sequences (ESC[...)
      .replace(/\x1b\[[0-9;]*[A-Za-z]/g, '')
      // Remove other escape sequences (ESC(...)
      .replace(/\x1b\([0-9;]*[A-Za-z]/g, '')
      // Remove CSI sequences
      .replace(/\x1b\[[\x30-\x3f]*[\x20-\x2f]*[\x40-\x7e]/g, '')
      // Remove all control characters (0x00-0x1F) except newline (0x0A) and tab (0x09)
      // This includes carriage return (0x0D) which we handle separately
      .replace(/[\x00-\x08\x0B-\x1F\x7F-\x9F]/g, '')
      // Remove any remaining replacement characters
      .replace(/\uFFFD/g, '')
  );
};

const getEspLogInfo = (
  data: string,
): {
  data: string;
  type: LogEntry['type'];
} => {
  // JSON frames are already escaped; skip terminal cleanup so Unicode survives.
  if (data.startsWith('{')) return { data: data.trimEnd(), type: 'info' };
  // Convert carriage returns to newlines for proper display
  const normalizedData = data.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
  const cleanedData = removeAnsiEscapeCodes(normalizedData.trimEnd());
  const type = getLogLevel(cleanedData);

  return {
    data: removeLogLevelPrefix(cleanedData),
    type,
  };
};

// Return true if the listener handled the log, false to ignore it
export type LogListener = (data: string, type: LogEntry['type']) => boolean;

export class ESPService {
  private espLoader: ESPLoader | null = null;
  private terminal?: TerminalService;
  private isConnecting: boolean = false;
  private bootloaderReady = false;
  private isFlashing = false;
  private commandGeneration = 0;
  private commandWrites: Promise<void> = Promise.resolve();
  private consoleTransactions: Promise<void> = Promise.resolve();
  private consoleFrame?: (data: string) => boolean;
  private cancelConsoleTransaction?: () => void;
  private consoleNeedsResync = false;
  private monitorSerial: boolean = false;
  private logBuffer: string = '';
  private logDecoder = new TextDecoder();
  private port: SerialPort | null = null;

  private logListeners: Array<LogListener> = [
    (d, t) => {
      this.log(d, t);

      return true;
    },
  ];

  public onReboot?: () => void;
  public onDisconnect?: () => void;

  private stacktraceService: StacktraceService = new StacktraceService();

  constructor(terminal?: TerminalService) {
    this.terminal = terminal;
  }

  private handlePortDisconnect = async () => {
    this.log('Device disconnected unexpectedly', 'error');
    await this.disconnect();
    if (this.onDisconnect) {
      this.onDisconnect();
    }
  };

  public async setElf(file: File | null) {
    if (file) {
      await this.stacktraceService.setElfFile(file);
      this.log('ELF file loaded for backtrace decoding', 'success');
    } else {
      // Maybe clear it? The service doesn't have clear method yet but overriding works.
      // For now do nothing or we could add clear to StacktraceService
    }
  }

  public addLogListener = (listener: LogListener) => {
    this.logListeners.unshift(listener);
  };

  public removeLogListener = (listener: LogListener) => {
    this.logListeners = this.logListeners.filter((l) => l !== listener);
  };

  public log = (message: string, type: 'info' | 'error' | 'success' = 'info') => {
    if (!message.replace('pubconsole>', '').trim()) {
      return;
    }
    this.terminal?.writeLine(message, type);
  };

  private emitToListeners = (...args: Parameters<LogListener>) => {
    if (this.consoleFrame?.(args[0])) return;
    for (const listener of this.logListeners) {
      if (listener(...args)) {
        break; // Break on first listener that marks log as handled
      }
    }
  };

  private processEspLog = async (data: string | Uint8Array) => {
    // Always append to buffer
    if (typeof data === 'string') {
      this.logBuffer += data;
    } else {
      this.logBuffer += this.logDecoder.decode(data, { stream: true });
    }

    // Process complete lines
    while (this.logBuffer.includes('\n')) {
      const splitIndex = this.logBuffer.indexOf('\n');
      let line = this.logBuffer.slice(0, splitIndex);
      this.logBuffer = this.logBuffer.slice(splitIndex + 1);

      // Another task's log can follow the prompt on the same line.
      const cleaned = removeAnsiEscapeCodes(line).trimStart();
      if (cleaned.startsWith('pubconsole>')) {
        this.emitToListeners('pubconsole>', 'info');
        line = cleaned.slice('pubconsole>'.length).trimStart();
      }

      const logInfo = getEspLogInfo(line);
      if (logInfo.data) {
        this.emitToListeners(logInfo.data, logInfo.type);
        // JSON values may contain "rst:" or "Backtrace:"; they aren't diagnostics.
        if (logInfo.data.startsWith('{')) continue;

        // Check for backtrace
        if (logInfo.data.includes('Backtrace:')) {
          console.log('[DEBUG] Backtrace detected in buffered line:', logInfo.data);
          console.log('[DEBUG] ELF loaded:', this.stacktraceService.isElfLoaded());
          if (this.stacktraceService.isElfLoaded()) {
            try {
              const decoded = await this.stacktraceService.decode(logInfo.data);
              console.log('[DEBUG] Decoded result:', decoded);
              this.log(decoded, 'info');
            } catch (error) {
              console.error('[DEBUG] Decode error:', error);
              this.log(`Backtrace decode error: ${error}`, 'error');
            }
          } else {
            this.log('Backtrace detected but no ELF file loaded.', 'info');
            // If we have backtrace but no ELF, try to auto-download if we have version info
            // But we don't have access to deviceInfo here easily unless we store it
          }
        }

        // Matches "rst:0x" or "rst: 0x" anywhere in the line
        if (
          logInfo.data.includes('rst:0x') ||
          logInfo.data.includes('rst: 0x') ||
          logInfo.data.includes('rst:')
        ) {
          console.log('[DEBUG] Reboot detected (buffered):', logInfo.data);
          this.onReboot?.();
        }
      }
    }

    // Check for prompt in remaining buffer
    // Prompts don't end with newline, so they sit in the buffer
    // We check if the trimmed buffer ends with our expected prompt
    const cleanedBuffer = removeAnsiEscapeCodes(this.logBuffer).trim();
    if (cleanedBuffer.endsWith('pubconsole>') || cleanedBuffer === 'pubconsole>') {
      this.emitToListeners('pubconsole>', 'info');
      this.logBuffer = ''; // Clear buffer after detecting prompt to be clean for next input
    }
  };

  getVersionInfo = async (): Promise<{
    version: string;
    variant: string;
    hardware: string;
  }> => {
    const timeout = 5000;
    let version: string | null = null;
    let variant: string | null = null;
    let hardware: string | null = null;

    return this.withConsoleTransaction(
      (transport) =>
        new Promise((resolve, reject) => {
          const timeoutId = setTimeout(() => {
            transport.removeLogListener(versionLogListener);
            reject(new Error('Timeout while waiting for version response'));
          }, timeout);

          // Request firmware info
          this.log('Fetching firmware information...');
          const versionLogListener: LogListener = (data) => {
            if (data.toLocaleLowerCase().startsWith('version:')) {
              // regex to match variant
              version = data.replace(/^version:\s*/i, '').trim();
            }

            if (data.toLowerCase().startsWith('variant:')) {
              variant = data.replace(/^variant:\s*/i, '').trim();
            }

            if (data.toLowerCase().startsWith('hardware:')) {
              hardware = data.replace(/^hardware:\s*/i, '').trim();
            }

            if (data === 'pubconsole>' && version && variant && hardware) {
              clearTimeout(timeoutId);
              transport.removeLogListener(versionLogListener);
              this.log('Version info successfully loaded');
              resolve({ version, variant, hardware });
              return true;
            }

            return true; // Mark log as handled
          };
          transport.addLogListener(versionLogListener);
          transport.sendCommand('version').catch((error) => {
            clearTimeout(timeoutId);
            transport.removeLogListener(versionLogListener);
            reject(error);
          });
        }),
    );
  };

  checkCoredump = async (): Promise<boolean> => {
    return this.withConsoleTransaction(
      (transport) =>
        new Promise((resolve) => {
          const timeout = 2000;
          const timeoutId = setTimeout(() => {
            transport.removeLogListener(coreDumpListener);
            resolve(false);
          }, timeout);

          const coreDumpListener: LogListener = (data) => {
            if (data.includes('coredump: found')) {
              clearTimeout(timeoutId);
              transport.removeLogListener(coreDumpListener);
              this.log('Core dump detected on device.', 'info');
              resolve(true);
              return true;
            }
            if (data.includes('coredump: none')) {
              clearTimeout(timeoutId);
              transport.removeLogListener(coreDumpListener);
              resolve(false);
              return true;
            }
            return false;
          };

          transport.addLogListener(coreDumpListener);
          transport.sendCommand('coredump_info').catch(() => {
            clearTimeout(timeoutId);
            transport.removeLogListener(coreDumpListener);
            resolve(false);
          });
        }),
    );
  };

  connect = async (): Promise<{
    connected: boolean;
    chipId: string;
    macAddress: string;
    version: string;
    variant: string;
    hardware: string;
    hasCoredump: boolean;
    hasFirmware: boolean;
  }> => {
    if (this.isConnecting) {
      throw new Error('Connection already in progress');
    }

    try {
      this.isConnecting = true;
      ++this.commandGeneration;
      this.bootloaderReady = false;
      this.log('Requesting serial port...');

      if (!navigator.serial) {
        throw new Error(
          'Web Serial API not supported in this browser. Please use a compatible browser like Chrome or Edge.',
        );
      }

      const port = await navigator.serial.requestPort();
      this.port = port;
      this.port.addEventListener('disconnect', this.handlePortDisconnect);

      const transport = new Transport(port, true);

      this.log('Initializing connection...');
      const loaderOptions: LoaderOptions = {
        transport,
        baudrate: 115200,
        romBaudrate: 115200,
        terminal: {
          clean: () => this.terminal?.clear(),
          writeLine: this.processEspLog, // TODO - insert newline?
          write: this.processEspLog,
        },
      };

      const loader = new ESPLoader(loaderOptions);

      await loader.main();
      await loader.sync();

      this.log('Detecting chip...');
      const chipId = await loader.chip.getChipDescription(loader);
      this.log(`Found ${chipId}`, 'success');

      this.log('Reading MAC address...');
      const macAddress = await loader.chip.readMac(loader);
      this.log(`MAC address: ${macAddress.toUpperCase()}`, 'success');

      this.log('Reading Chip Description...');
      const chipDescription = await loader.chip.getChipDescription(loader);
      this.log(`Chip Description: ${chipDescription}`, 'success');

      this.log('Reading Chip Features...');
      const chipFeatures = await loader.chip.getChipFeatures(loader);
      this.log(`Chip Features: ${chipFeatures}`, 'success');

      this.log('Reading Crystal Frequency...');
      const crystalFreq = await loader.chip.getCrystalFreq(loader);
      this.log(`Crystal Frequency: ${crystalFreq}`, 'success');

      // Determine whether the chip already holds valid firmware. We're still in
      // bootloader (stub) mode here, so we can read flash directly. A fresh chip
      // has no application, so rebooting into "normal" mode would only boot-loop
      // and the version query below would hang. When the chip is blank we stay
      // in bootloader mode so the user can perform a first-time install.
      this.log('Checking for existing firmware...');
      const hasFirmware = await this.hasValidFirmware(loader);

      let version: string = '';
      let variant: string = '';
      let hardware: string = '';
      let hasCoredump: boolean = false;

      if (!hasFirmware) {
        // Reuse the synced stub: resetting a blank ESP32-S3 can drop its native USB.
        this.espLoader = loader;
        this.bootloaderReady = true;
        this.log(
          'No firmware detected. Device is in bootloader mode and ready for a first-time install.',
          'success',
        );
      } else {
        this.log('Rebooting into normal mode...');
        await loader.hardReset();
        await loader.transport.disconnect();
        await delay(1000); // Give device time to boot

        // Reconnect in normal mode to get firmware info
        await transport.connect(115200);
        this.espLoader = loader;
        this.addSerialMonitor();
        // Wait for device to stabilize
        await delay(2000);

        try {
          const res = await this.getVersionInfo();
          version = res.version;
          variant = res.variant;
          hardware = res.hardware;

          // Check for core dump
          hasCoredump = await this.checkCoredump().catch((e) => {
            console.error(e);
            return false;
          });
        } catch (e) {
          this.log(
            `Connection failed: ${e instanceof Error ? e.message : 'Unknown error'}`,
            'error',
          );
        }
      }

      const info = {
        connected: true,
        chipId: chipId.toUpperCase(),
        macAddress: macAddress.toUpperCase(),
        version,
        variant,
        hardware,
        hasCoredump,
        hasFirmware,
      };

      this.log(
        hasFirmware
          ? `Device ready: ${info.chipId} running ${variant} v${version}`
          : `Device ready: ${info.chipId} (no firmware — ready to flash)`,
        'success',
      );
      return info;
    } catch (error) {
      this.log(
        `Connection failed: ${error instanceof Error ? error.message : 'Unknown error'}`,
        'error',
      );
      await this.disconnect();
      throw error;
    } finally {
      this.isConnecting = false;
    }
  };

  // Detect whether the chip already contains valid firmware. Must be called
  // while the loader is in bootloader/stub mode (i.e. right after main()/sync()).
  // A programmed ESP has the ESP image magic byte (0xE9) at the start of the
  // bootloader region (0x0); an erased/fresh chip reads 0xFF everywhere.
  private async hasValidFirmware(loader: ESPLoader): Promise<boolean> {
    try {
      const data = await loader.readFlash(0x0, 16);
      const allErased = data.every((b) => b === 0xff);
      const hasMagic = data[0] === 0xe9;
      return hasMagic && !allErased;
    } catch (e) {
      // If we can't read the flash for any reason, assume firmware may be
      // present so we don't skip version detection on a healthy device.
      this.log(
        `Could not read flash to detect firmware (${e instanceof Error ? e.message : 'unknown error'}); assuming firmware present.`,
        'info',
      );
      return true;
    }
  }

  private encodeCommand(command: string): Uint8Array {
    const encoder = new TextEncoder();
    return encoder.encode(command + '\n');
  }

  private invalidateConsoleConnection() {
    const wasConnected = this.isConnected();
    void this.disconnect();
    if (wasConnected) this.onDisconnect?.();
  }

  // Hold the console until its prompt returns, so late output never answers the next command.
  withConsoleTransaction<T>(
    operation: (transport: SettingsTransport) => Promise<T>,
    signal?: AbortSignal,
    silentDrain = true,
  ): Promise<T> {
    const generation = this.commandGeneration;
    const loader = this.espLoader;
    return new Promise<T>((resolve, reject) => {
      const abort = () => reject(new Error('Settings request cancelled'));
      signal?.addEventListener('abort', abort, { once: true });
      const run = async () => {
        if (signal?.aborted) {
          abort();
          return;
        }
        if (!loader || this.espLoader !== loader || generation !== this.commandGeneration) {
          reject(new Error('Connection changed before the command could be sent'));
          return;
        }
        if (this.bootloaderReady || this.isFlashing) {
          reject(
            new Error(
              'Device is in bootloader mode. Install firmware before using console commands.',
            ),
          );
          return;
        }
        if (this.consoleNeedsResync) {
          try {
            await this.resyncConsole();
          } catch (error) {
            reject(error);
            return;
          }
          if (signal?.aborted) {
            abort();
            return;
          }
        }
        let sent = false;
        let finished = false;
        let promptSeen = false;
        let finishPrompt = () => {};
        let interrupt = () => {};
        const prompt = new Promise<void>((done) => {
          finishPrompt = done;
        });
        const interrupted = new Promise<void>((done) => {
          interrupt = done;
        });
        const listeners = new Set<LogListener>();
        const frame = (data: string) => {
          if (data.trim() === 'pubconsole>') {
            promptSeen = true;
            finishPrompt();
          }
          return finished && silentDrain;
        };
        const cancel = () => {
          reject(new Error('Console connection changed'));
          interrupt();
          finishPrompt();
        };
        this.consoleFrame = frame;
        this.cancelConsoleTransaction = cancel;
        const timer = setTimeout(() => {
          const error = new Error(
            'Console did not return to its prompt. Waiting for the device before sending more commands.',
          );
          this.log(error.message, 'error');
          this.consoleNeedsResync = true;
          reject(error);
          interrupt();
          finishPrompt();
        }, 7000);
        const transport: SettingsTransport = {
          addLogListener: (listener) => {
            listeners.add(listener);
            this.addLogListener(listener);
          },
          removeLogListener: (listener) => {
            listeners.delete(listener);
            this.removeLogListener(listener);
          },
          sendCommand: async (command, silent) => {
            sent = true;
            try {
              await this.writeCommand(command, silent);
            } catch (error) {
              // A partial/failed write leaves command framing uncertain.
              reject(error);
              finishPrompt();
              this.invalidateConsoleConnection();
              throw error;
            }
          },
        };
        try {
          const response = Promise.resolve()
            .then(() => operation(transport))
            .then(resolve, reject)
            .finally(() => {
              finished = true;
            });
          await Promise.race([response, interrupted]);
          if (sent && !promptSeen) await prompt;
        } finally {
          clearTimeout(timer);
          for (const listener of listeners) this.removeLogListener(listener);
          if (this.consoleFrame === frame) this.consoleFrame = undefined;
          if (this.cancelConsoleTransaction === cancel) this.cancelConsoleTransaction = undefined;
        }
      };
      this.consoleTransactions = this.consoleTransactions
        .then(run)
        .catch(reject)
        .finally(() => {
          signal?.removeEventListener('abort', abort);
        });
    });
  }

  // A bare newline prints a fresh prompt; output before the last prompt is stale.
  private async resyncConsole(): Promise<void> {
    let seen = false;
    let quiet: ReturnType<typeof setTimeout> | undefined;
    let settle = () => {};
    let fail: (error: Error) => void = () => {};
    const synced = new Promise<void>((resolve, reject) => {
      settle = resolve;
      fail = reject;
    });
    synced.catch(() => {});
    const busy = new Error('Console busy, waiting for the device. Try again shortly.');
    const deadline = setTimeout(() => (seen ? settle() : fail(busy)), 3000);
    const frame = (data: string) => {
      if (data.trim() !== 'pubconsole>') return false;
      seen = true;
      clearTimeout(quiet);
      quiet = setTimeout(settle, 250);
      return true;
    };
    const cancel = () => fail(new Error('Console connection changed'));
    this.consoleFrame = frame;
    this.cancelConsoleTransaction = cancel;
    try {
      try {
        await this.writeCommand('', true);
      } catch (error) {
        this.invalidateConsoleConnection();
        throw error;
      }
      await synced;
      this.consoleNeedsResync = false;
      this.log('Console responding again');
    } finally {
      clearTimeout(deadline);
      clearTimeout(quiet);
      if (this.consoleFrame === frame) this.consoleFrame = undefined;
      if (this.cancelConsoleTransaction === cancel) this.cancelConsoleTransaction = undefined;
    }
  }

  sendCommand(command: string, silent = false): Promise<void> {
    return this.withConsoleTransaction(
      (transport) => transport.sendCommand(command, silent),
      undefined,
      silent,
    );
  }

  private async writeCommand(command: string, silent: boolean = false): Promise<void> {
    if (!this.espLoader || !this.isConnected()) {
      throw new Error('Device not connected');
    }

    if (this.bootloaderReady || this.isFlashing) {
      throw new Error(
        'Device is in bootloader mode. Install firmware before using console commands.',
      );
    }

    const loader = this.espLoader;
    const generation = this.commandGeneration;
    // Web Serial allows one writer at a time, so queue whole commands.
    const write = this.commandWrites.then(async () => {
      if (this.espLoader !== loader || generation !== this.commandGeneration) {
        throw new Error('Connection changed before the command could be sent');
      }
      await loader.transport.write(this.encodeCommand(command));
    });
    // A failed write must not block later commands.
    this.commandWrites = write.catch(() => {});
    try {
      await write;
      if (!silent) {
        this.log(`Sent command: ${command}`, 'info');
      }
    } catch (error) {
      this.log(
        `Failed to send command: ${error instanceof Error ? error.message : 'Unknown error'}`,
        'error',
      );
      throw error;
    }
  }

  async flash(
    firmware: FirmwareFiles,
    eraseFlash: boolean = true,
    onFlashProgess: (update: { status: string; progress: number }) => void,
  ): Promise<void> {
    if (this.isFlashing) {
      throw new Error('Firmware flash already in progress');
    }
    if (!this.espLoader) {
      throw new Error('Not connected to device');
    }

    const loader = this.espLoader;
    this.isFlashing = true;
    ++this.commandGeneration;
    this.cancelConsoleTransaction?.();
    try {
      await this.commandWrites;
      this.removeSerialMonitor();
      if (this.bootloaderReady) {
        this.log('Using existing bootloader connection...');
      } else {
        await delay(200); // Let the serial monitor stop before closing its port.
        await loader.transport.disconnect();
        this.log('Rebooting into bootloader...');
        await loader.main();
        await loader.sync();
        if (this.espLoader !== loader)
          throw new Error('Device disconnected while entering bootloader');
        this.bootloaderReady = true;
      }

      if (eraseFlash) {
        this.log('Erasing flash...');
        await loader.eraseFlash();
      }

      const files: Array<{
        data: string;
        address: number;
        name: string;
      }> = [];

      if (firmware.bootloader) {
        files.push({
          data: await this.readFileAsString(firmware.bootloader),
          address: 0x0,
          name: 'Bootloader',
        });
      }
      if (firmware.partitionTable) {
        files.push({
          data: await this.readFileAsString(firmware.partitionTable),
          address: 0x8000,
          name: 'Partition Table',
        });
      }

      if (firmware.application) {
        files.push({
          data: await this.readFileAsString(firmware.application),
          address: 0x10000,
          name: 'Application',
        });
      }

      this.log('Writing firmware...');
      await loader.writeFlash({
        fileArray: files.map(({ data, address }) => ({ data, address })),
        flashSize: 'keep',
        eraseAll: false, // Handled above
        compress: true,
        flashFreq: 'keep',
        flashMode: 'keep',
        reportProgress: (fileIndex: number, written: number, total: number) => {
          const progress = written / total;
          const overallProgress = (fileIndex + progress) / files.length;
          onFlashProgess?.({
            status: `Writing ${files[fileIndex].name}...`,
            progress: overallProgress,
          });
          this.log(`Writing ${files[fileIndex].name}: ${Math.round(progress * 100)}%`);
        },
      });

      this.log('Flash complete', 'success');
      this.log('Resetting device...');
      this.bootloaderReady = false;
      await loader.hardReset();
      this.log('Device reset and ready', 'success');
    } catch (error) {
      this.bootloaderReady = false;
      this.log(
        `Flash failed: ${error instanceof Error ? error.message : 'Unknown error'}`,
        'error',
      );
      throw error;
    } finally {
      this.isFlashing = false;
    }
  }

  private async readFileAsString(file: File): Promise<string> {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(reader.result as string);
      reader.onerror = () => reject(reader.error);
      reader.readAsBinaryString(file);
    });
  }

  isConnected(): boolean {
    return this.espLoader !== null;
  }

  addSerialMonitor() {
    this.logBuffer = '';
    this.logDecoder = new TextDecoder();
    const monitor = async () => {
      while (this.monitorSerial) {
        const val = await this.espLoader!.transport.rawRead();
        if (typeof val !== 'undefined') {
          this.processEspLog(val);
        } else {
          break;
        }
      }
      this.monitorSerial = false;
    };
    this.monitorSerial = true;
    monitor();
  }

  removeSerialMonitor() {
    this.monitorSerial = false;
  }

  async disconnect(): Promise<void> {
    ++this.commandGeneration;
    this.cancelConsoleTransaction?.();
    this.consoleNeedsResync = false;
    const loader = this.espLoader;
    const port = this.port;
    this.espLoader = null;
    this.port = null;
    this.bootloaderReady = false;
    this.removeSerialMonitor();
    if (port) {
      port.removeEventListener('disconnect', this.handlePortDisconnect);
    }

    if (loader) {
      try {
        await this.commandWrites;
        await loader.transport.disconnect();
      } catch {
        // Ignore disconnect errors
      }
      this.log('Disconnected from device');
    }
  }

  // Execute a command silently and capture output
  executeCommand = async (command: string, timeout = 2000): Promise<string[]> => {
    return this.withConsoleTransaction(
      (transport) =>
        new Promise((resolve, reject) => {
          const lines: string[] = [];
          const timeoutId = setTimeout(() => {
            transport.removeLogListener(listener);
            console.warn(`[executeCommand] Timeout waiting for prompt for command: "${command}"`);
            resolve(lines); // return what we have so far
          }, timeout);

          const listener: LogListener = (data) => {
            const trimmed = data.trim();
            if (trimmed === 'pubconsole>') {
              clearTimeout(timeoutId);
              transport.removeLogListener(listener);
              resolve(lines);
              return true;
            }
            // Filter out echo if present (simple check)
            if (trimmed !== command.trim()) {
              lines.push(data.trimEnd());
            }
            return true; // Swallow the log
          };

          transport.addLogListener(listener);
          transport.sendCommand(command, true).catch((error) => {
            clearTimeout(timeoutId);
            transport.removeLogListener(listener);
            reject(error);
          });
        }),
    );
  };

  getCompletions = async (prefix: string): Promise<string[]> => {
    // Don't autocomplete if empty or just whitespace
    if (!prefix || !prefix.trim()) return [];

    const lines = await this.executeCommand(`complete "${prefix}"`, 3000); // Increased timeout to 3s

    // Filter out empty lines or other noise if any
    return lines.filter(
      (l) =>
        l.trim().length > 0 &&
        !l.includes('Usage: complete') &&
        !l.startsWith('complete ') &&
        !l.includes('Unrecognized command'),
    );
  };
}
