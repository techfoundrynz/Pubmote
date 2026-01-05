/* eslint-disable no-control-regex */
import { ESPLoader, Transport, LoaderOptions } from "esptool-js";
import { delay } from "../utils/delay";
import { LogEntry, TerminalService } from "./terminal";
import { FirmwareFiles } from "../types";
import { StacktraceService } from "./stacktraceService";

const LogTypePrefixMap: Record<LogEntry["type"], Array<`${string} `>> = {
  info: ["I "],
  error: ["E "],
  success: ["I ", "W "],
};

const getLogLevel = (data: string): LogEntry["type"] => {
  for (const [type, prefixes] of Object.entries(LogTypePrefixMap)) {
    if (prefixes.some((prefix) => data.startsWith(prefix))) {
      return type as LogEntry["type"];
    }
  }
  return "info";
}

const removeLogLevelPrefix = (data: string): string => {
  for (const prefixes of Object.values(LogTypePrefixMap)) {
    for (const prefix of prefixes) {
      if (data.startsWith(prefix)) {
        return data.slice(prefix.length);
      }
    }
  }
  return data;
}

const removeAnsiEscapeCodes = (data: string): string => {
  return data
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
    .replace(/\uFFFD/g, '');
};

const getEspLogInfo = (
  data: string
): {
  data: string;
  type: LogEntry["type"];
} => {
  // Convert carriage returns to newlines for proper display
  const normalizedData = data.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
  const cleanedData = removeAnsiEscapeCodes(normalizedData.trim());
  const type = getLogLevel(cleanedData);

  return {
    data: removeLogLevelPrefix(cleanedData),
    type,
  };
};

// Return true if the listener handled the log, false to ignore it
export type LogListener = (data: string, type: LogEntry["type"]) => boolean;

export class ESPService {
  private espLoader: ESPLoader | null = null;
  private terminal?: TerminalService;
  private isConnecting: boolean = false;
  private monitorSerial: boolean = false;
  private logBuffer: string = "";

  private logListeners: Array<LogListener> = [(d, t) => {
    this.terminal?.writeLine(
      d, t
    );

    return true;
  }];


  private stacktraceService: StacktraceService = new StacktraceService();

  constructor(terminal?: TerminalService) {
    this.terminal = terminal;
  }

  public async setElf(file: File | null) {
    if (file) {
      await this.stacktraceService.setElfFile(file);
      this.log("ELF file loaded for backtrace decoding", "success");
    } else {
      // Maybe clear it? The service doesn't have clear method yet but overriding works.
      // For now do nothing or we could add clear to StacktraceService
    }
  }

  public addLogListener = (listener: LogListener) => {
    this.logListeners.unshift(listener);
  }

  public removeLogListener = (listener: LogListener) => {
    this.logListeners = this.logListeners.filter((l) => l !== listener);
  }

  public log = (message: string, type: "info" | "error" | "success" = "info") => {
    this.terminal?.writeLine(message, type);
  }

  private emitToListeners = (...args: Parameters<LogListener>) => {
    for (const listener of this.logListeners) {
      if (listener(...args)) {
        break; // Break on first listener that marks log as handled
      }
    }
  }

  private processEspLog = async (data: string | Uint8Array) => {
    if (typeof data === "string") {
      const logInfo = getEspLogInfo(data);
      if (logInfo.data) {
        this.emitToListeners(logInfo.data, logInfo.type);

        // Check for backtrace
        if (logInfo.data.includes("Backtrace:")) {
          console.log("[DEBUG] Backtrace detected in line:", logInfo.data);
          console.log("[DEBUG] ELF loaded:", this.stacktraceService.isElfLoaded());
          if (this.stacktraceService.isElfLoaded()) {
            try {
              const decoded = await this.stacktraceService.decode(logInfo.data);
              console.log("[DEBUG] Decoded result:", decoded);
              this.log(decoded, "info");
            } catch (error) {
              console.error("[DEBUG] Decode error:", error);
              this.log(`Backtrace decode error: ${error}`, "error");
            }
          } else {
            this.log("Backtrace detected but no ELF file loaded.", "info");
          }
        }
      }
    } else {
      // If log message is split into multiple chunks, buffer it until newline
      this.logBuffer += new TextDecoder().decode(data);
      if (!this.logBuffer.includes("\n")) {
        return;
      }

      while (this.logBuffer.includes("\n")) {
        const [line, ...rest] = this.logBuffer.split("\n");
        this.logBuffer = rest.join("\n");

        const logInfo = getEspLogInfo(line);
        if (logInfo.data) {
          this.emitToListeners(logInfo.data, logInfo.type);

          // Check for backtrace
          if (logInfo.data.includes("Backtrace:")) {
            console.log("[DEBUG] Backtrace detected in buffered line:", logInfo.data);
            console.log("[DEBUG] ELF loaded:", this.stacktraceService.isElfLoaded());
            if (this.stacktraceService.isElfLoaded()) {
              try {
                const decoded = await this.stacktraceService.decode(logInfo.data);
                console.log("[DEBUG] Decoded result:", decoded);
                this.log(decoded, "info");
              } catch (error) {
                console.error("[DEBUG] Decode error:", error);
                this.log(`Backtrace decode error: ${error}`, "error");
              }
            } else {
              this.log("Backtrace detected but no ELF file loaded.", "info");
            }
          }
        }
      }
    }
  }

  getVersionInfo = async (): Promise<{ version: string; variant: string, hardware: string }> => {
    const timeout = 5000;
    let version: string | null = null;
    let variant: string | null = null;
    let hardware: string | null = null;

    return new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        this.removeLogListener(versionLogListener);
        reject(new Error("Timeout while waiting for version response"));
      }, timeout);

      // Request firmware info
      this.log("Fetching firmware information...");
      const versionLogListener: LogListener = (data) => {
        if (data.toLocaleLowerCase().startsWith("version:")) {
          // regex to match variant
          version = data.replace(/^version:\s*/i, "").trim();
        }

        if (data.toLowerCase().startsWith("variant:")) {
          variant = data.replace(/^variant:\s*/i, "").trim();
        }

        if (data.toLowerCase().startsWith("hardware:")) {
          hardware = data.replace(/^hardware:\s*/i, "").trim();
        }

        if (data === 'pubconsole>' && version && variant && hardware) {
          clearTimeout(timeoutId);
          this.removeLogListener(versionLogListener);
          this.log("Version info successfully loaded");
          resolve({ version, variant, hardware });
        }

        return true; // Mark log as handled
      };
      this.addLogListener(versionLogListener);
      this.sendCommand("version")
    });
  }

  checkCoredump = async (): Promise<boolean> => {
    return new Promise((resolve) => {
      const timeout = 2000;
      const timeoutId = setTimeout(() => {
        this.removeLogListener(coreDumpListener);
        resolve(false);
      }, timeout);

      const coreDumpListener: LogListener = (data) => {
        if (data.includes("coredump: found")) {
          clearTimeout(timeoutId);
          this.removeLogListener(coreDumpListener);
          this.log("Core dump detected on device.", "info");
          resolve(true);
          return true;
        }
        if (data.includes("coredump: none")) {
          clearTimeout(timeoutId);
          this.removeLogListener(coreDumpListener);
          resolve(false);
          return true;
        }
        return false;
      };

      this.addLogListener(coreDumpListener);
      this.sendCommand("coredump_info");
    });
  }

  connect = async (): Promise<{
    connected: boolean;
    chipId: string;
    macAddress: string;
    version: string;
    variant: string;
    hardware: string;
    hasCoredump: boolean;
  }> => {
    if (this.isConnecting) {
      throw new Error("Connection already in progress");
    }

    try {
      this.isConnecting = true;
      this.log("Requesting serial port...");

      if (!navigator.serial) {
        throw new Error("Web Serial API not supported in this browser. Please use a compatible browser like Chrome or Edge.");
      }

      const port = await navigator.serial.requestPort();
      const transport = new Transport(port, true);

      this.log("Initializing connection...");
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

      // ... (existing loader.main(), loader.sync(), chip info reading ...)
      // I need to keep the context lines for the replace, so I will target the specific block I'm changing
      // But wait, the previous tool call modified the file, line numbers might shifted.
      // I will rely on the Context match.
      await loader.main();
      await loader.sync();

      this.log("Detecting chip...");
      const chipId = await loader.chip.getChipDescription(loader);
      this.log(`Found ${chipId}`, "success");

      this.log("Reading MAC address...");
      const macAddress = await loader.chip.readMac(loader);
      this.log(`MAC address: ${macAddress.toUpperCase()}`, "success");

      this.log("Reading Chip Description...");
      const chipDescription = await loader.chip.getChipDescription(loader);
      this.log(`Chip Description: ${chipDescription}`, "success");

      this.log("Reading Chip Features...");
      const chipFeatures = await loader.chip.getChipFeatures(loader);
      this.log(`Chip Features: ${chipFeatures}`, "success");

      this.log("Reading Crystal Frequency...");
      const crystalFreq = await loader.chip.getCrystalFreq(loader);
      this.log(`Crystal Frequency: ${crystalFreq}`, "success");

      this.log("Rebooting into normal mode...");
      await loader.hardReset();
      await loader.transport.disconnect();
      await delay(1000); // Give device time to boot

      // Reconnect in normal mode to get firmware info
      await transport.connect(115200);
      this.espLoader = loader;
      this.addSerialMonitor();
      // Wait for device to stabilize
      await delay(2000);
      let version: string = "";
      let variant: string = "";
      let hardware: string = "";
      let hasCoredump: boolean = false;

      try {
        const res = await this.getVersionInfo();
        version = res.version;
        variant = res.variant;
        hardware = res.hardware;

        // Check for core dump
        hasCoredump = await this.checkCoredump().catch(e => {
          console.error(e);
          return false;
        });
      } catch (e) {
        this.log(
          `Connection failed: ${e instanceof Error ? e.message : "Unknown error"
          }`,
          "error"
        );
      }

      const info = {
        connected: true,
        chipId: chipId.toUpperCase(),
        macAddress: macAddress.toUpperCase(),
        version,
        variant,
        hardware,
        hasCoredump,
      };

      this.log(
        `Device ready: ${info.chipId} running ${variant} v${version}`,
        "success"
      );
      return info;
    } catch (error) {
      this.log(
        `Connection failed: ${error instanceof Error ? error.message : "Unknown error"
        }`,
        "error"
      );
      await this.disconnect();
      throw error;
    } finally {
      this.isConnecting = false;
    }
  }

  private async readResponse(timeout = 1000): Promise<string> {
    let response = "";
    const startTime = Date.now();

    while (Date.now() - startTime < timeout) {
      const data = await this.espLoader?.transport.rawRead(timeout);
      if (data) {
        response += data;
        if (response.includes("\n")) {
          break;
        }
      }
      await delay(10);
    }

    return response;
  }

  private encodeCommand(command: string): Uint8Array {
    const encoder = new TextEncoder();
    return encoder.encode(command + "\n");
  }

  async sendCommand(command: string): Promise<void> {
    if (!this.espLoader || !this.isConnected()) {
      throw new Error("Device not connected");
    }

    try {
      await this.espLoader.transport.write(this.encodeCommand(command + "\n"));
      this.log(`Sent command: ${command}`, "info");
    } catch (error) {
      this.log(
        `Failed to send command: ${error instanceof Error ? error.message : "Unknown error"
        }`,
        "error"
      );
      throw error;
    }
  }

  async flash(firmware: FirmwareFiles, eraseFlash: boolean = true, onFlashProgess: (update: {
    status: string;
    progress: number;
  }) => void): Promise<void> {
    if (!this.espLoader) {
      throw new Error("Not connected to device");
    }

    try {
      this.removeSerialMonitor();
      await delay(200); // Give device time to boot
      await this.espLoader.transport.disconnect();
      this.log("Rebooting into bootloader...");
      await this.espLoader.main();
      await this.espLoader.sync();

      if (eraseFlash) {
        this.log("Erasing flash...");
        await this.espLoader.eraseFlash();
      }


      const files: Array<{
        data: string;
        address: number;
        name: string;
      }> = [
        ];

      if (firmware.bootloader) {
        files.push(
          {
            data: await this.readFileAsString(firmware.bootloader),
            address: 0x0,
            name: "Bootloader",
          });

      }
      if (firmware.partitionTable) {
        files.push(
          {
            data: await this.readFileAsString(firmware.partitionTable),
            address: 0x8000,
            name: "Partition Table",
          });
      }

      if (firmware.application) {
        files.push({
          data: await this.readFileAsString(firmware.application),
          address: 0x10000,
          name: "Application",
        });
      }


      this.log("Writing firmware...");
      await this.espLoader.writeFlash({
        fileArray: files.map(({ data, address }) => ({ data, address })),
        flashSize: "keep",
        eraseAll: false, // Handled above
        compress: true,
        flashFreq: "keep",
        flashMode: "keep",
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

      this.log("Flash complete", "success");
      this.log("Resetting device...");
      await this.espLoader.hardReset();
      this.log("Device reset and ready", "success");
    } catch (error) {
      this.log(
        `Flash failed: ${error instanceof Error ? error.message : "Unknown error"
        }`,
        "error"
      );
      throw error;
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
    const monitor = async () => {
      while (this.monitorSerial) {
        const val = await this.espLoader!.transport.rawRead();
        if (typeof val !== "undefined") {
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
    this.removeSerialMonitor();
    if (this.espLoader) {
      try {
        await this.espLoader.transport.disconnect();
      } catch (error) {
        // Ignore disconnect errors
      }
      this.espLoader = null;
      this.log("Disconnected from device");
    }
  }
}
