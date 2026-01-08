// DeviceInfo.tsx
import React from "react";
import { Box, Tag, Cpu, Wifi } from "lucide-react";
import { Badge } from './ui/Badge';
import { DeviceInfoData, FlashProgress } from "../types";

import { Terminal } from "./Terminal";
import { TerminalService } from "../services/terminal";

interface Props {
  deviceInfo: DeviceInfoData;
  onSendCommand: (command: string) => Promise<void>;
  terminal: TerminalService;
  onViewCoredump: () => Promise<void>;
  onClearCoredump: () => Promise<void>;
  onLoadElf: (file: File) => Promise<void>;
  onDownloadElf: (isManual?: boolean) => Promise<void>;
  isElfLoaded?: boolean;
  updateAvailable?: boolean;
  flashProgress?: FlashProgress;
  getCompletions?: (prefix: string) => Promise<string[]>;
}

export function DeviceInfo({
  deviceInfo,
  onSendCommand,
  terminal,
  onViewCoredump,
  onClearCoredump,
  onLoadElf,
  onDownloadElf,
  isElfLoaded,
  updateAvailable = false,
  flashProgress,
  getCompletions
}: Props) {
  
  return (
    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6 flex flex-col h-full">
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-xl font-semibold">Device Information</h2>
        <Badge variant={deviceInfo.connected ? 'success' : 'destructive'} size="lg">
          {deviceInfo.connected ? 'Connected' : 'Disconnected'}
        </Badge>
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
                  <Badge variant="default">
                    Update Available
                  </Badge>
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
        getCompletions={getCompletions}
      />
    </div>
  );
}
