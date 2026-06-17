import init, { decode } from './esp_stacktrace_decoder_rs.js';

export class StacktraceService {
  private elfContent: Uint8Array | null = null;
  private isInitialized = false;

  async initialize() {
    if (!this.isInitialized) {
      await init("/esp-stacktrace-decoder/esp_stacktrace_decoder_rs_bg.wasm");
      this.isInitialized = true;
    }
  }

  async setElfFile(file: File) {
    const buffer = await file.arrayBuffer();
    this.elfContent = new Uint8Array(buffer);
  }

  isElfLoaded(): boolean {
    return this.elfContent !== null;
  }

  async decode(backtrace: string): Promise<string> {
    if (!this.isInitialized) {
      await this.initialize();
    }

    if (!this.elfContent) {
      return "ELF file not loaded. Cannot decode backtrace.";
    }

    try {
      // The backtrace string might contain "Backtrace: " prefix, the decoder might expect it or handle it.
      // Based on typical usage, we usually pass the full "Backtrace: ..." string or the list of addresses.
      // Let's pass the whole string.

      const addresses = decode(this.elfContent, backtrace);

      if (!addresses || addresses.length === 0) {
        return "No addresses decoded.";
      }

      let output = "\nDecoded Backtrace:\n";
      for (const addr of addresses) {
        // addr is likely a DecodedAddress object with getters
        // check if it has the properties we expect
        const fn = addr.function_name || "??";
        const loc = addr.location || "??:?";
        const address = addr.address ? `0x${addr.address.toString(16)}` : "??";

        output += `${address}: ${fn} at ${loc}\n`;

        // Free the object if necessary? The JS wrapper seems to use FinalizationRegistry, so manual free might not be strictly required in JS,
        // but the class has a .free() method. It's good practice if we want to avoid leaks in WASM memory.
        if (addr.free) addr.free();
      }
      return output;

    } catch (e) {
      console.error("Backtrace decoding failed:", e);
      return `Backtrace decoding failed: ${e}`;
    }
  }
}
