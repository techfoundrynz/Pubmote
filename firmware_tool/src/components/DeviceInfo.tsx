// DeviceInfo.tsx
import React from "react";
import { Cpu, Wifi, Tag, Box } from "lucide-react";
import { DeviceInfoData, FlashProgress } from "../types";

import { Terminal } from "./Terminal";
import { TerminalService } from "../services/terminal";

interface Props {
  deviceInfo: DeviceInfoData;
  onConnect: () => Promise<void>;
  onDisconnect: () => void;
  onSendCommand: (command: string) => Promise<void>;
  terminal: TerminalService;
  onViewCoredump: () => Promise<void>;
  onClearCoredump: () => Promise<void>;
  onLoadElf: (file: File) => Promise<void>;
  onDownloadElf: (isManual?: boolean) => Promise<void>;
  isElfLoaded?: boolean;
  updateAvailable?: boolean;
  flashProgress?: FlashProgress;
}

export function DeviceInfo({
  deviceInfo,
  onConnect,
  onDisconnect,
  onSendCommand,
  terminal,
  onViewCoredump,
  onClearCoredump,
  onLoadElf,
  onDownloadElf,
  isElfLoaded,
  updateAvailable = false,
  flashProgress,
}: Props) {
  
  return (
    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6 flex flex-col h-full">
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-semibold">Device Information</h2>
        <div className={`px-2 py-1 rounded text-xs font-medium border ${
          deviceInfo.connected 
            ? 'bg-[var(--badge-green-bg)] text-[var(--badge-green-text)] border-[var(--badge-green-border)]' 
            : 'bg-[var(--badge-red-bg)] text-[var(--badge-red-text)] border-[var(--badge-red-border)]'
        }`}>
          {deviceInfo.connected ? 'Connected' : 'Disconnected'}
        </div>
      </div>

        <div className="grid grid-cols-2 gap-2 mb-4">
          <div className="flex items-center gap-2 rounded-lg bg-[var(--color-bg-tertiary)] p-2.5 overflow-hidden">
            <Box className="h-4 w-4 text-blue-500 flex-shrink-0" />
            <div className="min-w-0">
              <div className="text-xs text-[var(--color-text-tertiary)] truncate">Hardware</div>
              <div className="text-base font-medium text-[var(--color-text-primary)] truncate" title={deviceInfo.hardware || "Unknown"}>
                {deviceInfo.hardware || "Unknown"}
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2 rounded-lg bg-[var(--color-bg-tertiary)] p-2.5 overflow-hidden">
            <Tag className="h-4 w-4 text-blue-500 flex-shrink-0" />
            <div className="min-w-0">
              <div className="text-xs text-[var(--color-text-tertiary)] truncate">Firmware</div>
              <div className="flex items-center gap-1.5">
                <div className="text-base font-medium text-[var(--color-text-primary)] truncate" title={`${deviceInfo.version || "Unknown"} (${deviceInfo.variant || "unknown"})`}>
                  {deviceInfo.version || "Unknown"}
                </div>
                {updateAvailable && (
                  <span className="px-1.5 py-0.5 text-[10px] font-medium bg-[var(--badge-blue-bg)] text-[var(--badge-blue-text)] border border-[var(--badge-blue-border)] rounded whitespace-nowrap">
                    Update Available
                  </span>
                )}
              </div>
              {/* Variant hidden in dense mode or tooltip used */}
            </div>
          </div>

          <div className="flex items-center gap-2 rounded-lg bg-[var(--color-bg-tertiary)] p-2.5 overflow-hidden">
            <Cpu className="h-4 w-4 text-blue-500 flex-shrink-0" />
            <div className="min-w-0">
              <div className="text-xs text-[var(--color-text-tertiary)] truncate">Chip</div>
              <div className="text-base font-medium text-[var(--color-text-primary)] truncate" title={deviceInfo.chipId || "Unknown"}>
                {deviceInfo.chipId || "Unknown"}
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2 rounded-lg bg-[var(--color-bg-tertiary)] p-2.5 overflow-hidden">
            <Wifi className="h-4 w-4 text-blue-500 flex-shrink-0" />
            <div className="min-w-0">
              <div className="text-xs text-[var(--color-text-tertiary)] truncate">MAC</div>
              <div className="text-base font-medium text-[var(--color-text-primary)] truncate" title={deviceInfo.macAddress || "Unknown"}>
                {deviceInfo.macAddress || "Unknown"}
              </div>
            </div>
          </div>
        </div>

      <Terminal
        terminal={terminal}
        onSendCommand={onSendCommand}
        disabled={!deviceInfo.connected}
        deviceInfo={deviceInfo}
        onViewCoredump={onViewCoredump}
        onClearCoredump={onClearCoredump}
        onLoadElf={onLoadElf}
        onDownloadElf={onDownloadElf}
        isElfLoaded={isElfLoaded}
        flashProgress={flashProgress}
      />
    </div>
  );
}
