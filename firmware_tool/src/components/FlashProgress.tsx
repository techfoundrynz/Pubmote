import React from 'react';
import { FlashProgress as FlashProgressType } from '../types';
import { CheckCircle, AlertCircle, Loader2, CircleDot } from 'lucide-react';

interface Props {
  progress: FlashProgressType;
  isDeviceConnected?: boolean;
  eraseFlash: boolean;
  onEraseFlashChange: (value: boolean) => void;
  hasFirmwareFiles?: boolean;
}

export function FlashProgress({ progress, isDeviceConnected = false, eraseFlash, onEraseFlashChange, hasFirmwareFiles = false }: Props) {
  const getStatusIcon = () => {
    switch (progress.status) {
      case 'idle':
        return <CircleDot className="h-6 w-6 text-gray-500" />;
      case 'complete':
        return <CheckCircle className="h-6 w-6 text-green-500" />;
      case 'error':
        return <AlertCircle className="h-6 w-6 text-red-500" />;
      default:
        return <Loader2 className="h-6 w-6 animate-spin text-blue-500" />;
    }
  };

  const getStatusText = () => {
    if (progress.status === 'idle' && !isDeviceConnected) {
      return 'Connect a device to begin flashing';
    }

    switch (progress.status) {
      case 'idle':
        return hasFirmwareFiles ? 'Device connected - Ready to flash' : 'Device connected - No files selected';
      case 'connecting':
        return 'Connecting to device...';
      case 'erasing':
        return 'Erasing flash...';
      case 'flashing':
        return 'Writing firmware...';
      case 'verifying':
        return 'Verifying flash...';
      case 'complete':
        return 'Flash complete!';
      case 'error':
        return progress.error || 'An error occurred';
      default:
        return 'Ready to flash';
    }
  };

  return (
    <div className="space-y-4 rounded-lg bg-[var(--color-bg-tertiary)] p-4">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          {getStatusIcon()}
          <span className="text-sm font-medium text-[var(--color-text-primary)]">
            {getStatusText()}
          </span>
        </div>
        <label className="flex items-center gap-2 text-sm text-[var(--color-text-primary)]">
          <input
            type="checkbox"
            checked={eraseFlash}
            onChange={(e) => onEraseFlashChange(e.target.checked)}
            disabled={!['complete', 'idle'].includes(progress.status)}
            className="rounded border-gray-600 text-blue-500 focus:ring-blue-500 focus:ring-offset-gray-900"
          />
          Erase flash
        </label>
      </div>

      {progress.status !== 'idle' &&
        progress.status !== 'complete' &&
        progress.status !== 'error' && (
          <div className="space-y-2">
            <div className="h-2 overflow-hidden rounded-full bg-[var(--color-bg-primary)]">
              <div
                className="h-full bg-blue-500 transition-all duration-300"
                style={{ width: `${progress.progress}%` }}
              />
            </div>
            <div className="text-right text-sm text-gray-500">
              {Math.round(progress.progress)}%
            </div>
          </div>
        )}
    </div>
  );
}