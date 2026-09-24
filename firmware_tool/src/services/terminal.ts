export interface LogEntry {
  message: string;
  type: "info" | "error" | "success";
  timestamp: string;
}

export class TerminalService {
  private logs: LogEntry[] = [];
  private subscribers: Set<(log: LogEntry | null) => void> = new Set();

  log(message: string, type: LogEntry["type"] = "info"): void {
    const entry: LogEntry = {
      message: message.trim(),
      type,
      timestamp: new Date().toLocaleTimeString(),
    };
    this.logs.push(entry);
    this.notifySubscribers(entry);
  }

  writeLine(message: string, type?: LogEntry["type"]): void {
    this.log(message, type);
  }

  subscribe(callback: (log: LogEntry | null) => void): () => void {
    this.subscribers.add(callback);
    return () => this.subscribers.delete(callback);
  }

  private notifySubscribers(log: LogEntry | null): void {
    this.subscribers.forEach((callback) => callback(log));
  }

  getLogs(): LogEntry[] {
    return [...this.logs];
  }

  clear(): void {
    this.logs = [];
    this.notifySubscribers(null);
  }
}
