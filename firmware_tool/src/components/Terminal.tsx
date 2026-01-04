import React, { useEffect, useRef } from 'react';
import { Terminal as TerminalIcon, Send, Trash2, Filter, Download, ArrowDownCircle, FileCode, Upload, Cloud } from 'lucide-react';
import { Dropdown } from './ui/Dropdown';
import { DeviceInfoData, FlashProgress } from '../types';
import { LogEntry, TerminalService } from '../services/terminal';

import { CoredumpBanner } from './CoredumpBanner';
import { cn } from '../utils/cn';

interface Props {
  terminal: TerminalService;
  onSendCommand: (command: string) => void;
  disabled?: boolean;
  deviceInfo?: DeviceInfoData;
  onViewCoredump?: () => Promise<void>;
  onClearCoredump?: () => Promise<void>;
  onLoadElf?: (file: File) => Promise<void>;
  onDownloadElf?: () => Promise<void>;
  isElfLoaded?: boolean;
  flashProgress?: FlashProgress;
}

export function Terminal({ 
  terminal, 
  onSendCommand, 
  disabled = false, 
  deviceInfo,
  onViewCoredump,
  onClearCoredump,
  onLoadElf,
  onDownloadElf,
  isElfLoaded,
  flashProgress
}: Props) {
  const [command, setCommand] = React.useState('');
  const commandBuffer = React.useRef<string[]>([]);
  const commandBufferIndex = React.useRef<number>(0);
  const [logs, setLogs] = React.useState<LogEntry[]>([]);
  const [autoScroll, setAutoScroll] = React.useState(true);
  const [enabledLogTypes, setEnabledLogTypes] = React.useState<string[]>([
    'info',
    'error',
    'success',
  ]);
  const terminalRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    // Subscribe to terminal data
    const dataHandler = (data: LogEntry | null) => {
      if (data === null) {
        setLogs([]);
      } else {

        if (data) {
          setLogs(prev => [...prev, data]);
        }
      }
    };

    const remove = terminal.subscribe(dataHandler);

    return () => {
      remove();
    };
  }, [terminal]);

  useEffect(() => {
    if (autoScroll && terminalRef.current) {
      terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
    }
  }, [logs, autoScroll]);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (command.trim()) {
      const finalCommand = command.trim();
      commandBuffer.current.push(finalCommand);
      commandBufferIndex.current = commandBuffer.current.length; // Reset index to the end
      onSendCommand(finalCommand);
      setCommand('');
    }
  };

  const clearLogs = () => {
    setLogs([]);
    terminal.clear();
  };

  const downloadLogs = () => {
    const deviceInfoText = deviceInfo?.connected
      ? `Device Information:
- Chip Type: ${deviceInfo.chipId || 'N/A'}
- MAC Address: ${deviceInfo.macAddress || 'N/A'}
- Firmware Version: ${deviceInfo.version || 'N/A'}
- Firmware Variant: ${deviceInfo.variant || 'N/A'}

`
      : 'Device not connected\n\n';

    const logsText = logs
      .filter(log => enabledLogTypes.includes(log.type))
      .map(log => `[${log.timestamp}] ${log.type.toUpperCase()}: ${log.message}`)
      .join('\n');
    
    const fullText = deviceInfoText + 'Terminal Logs:\n' + logsText;
    
    const blob = new Blob([fullText], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `pubmote-${new Date().toISOString().slice(0, 19).replace(/[:]/g, '-')}.log`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    if (e.target.files && e.target.files.length > 0 && onLoadElf) {
      onLoadElf(e.target.files[0]);
    }
    // Reset input so same file can be selected again
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  const filteredLogs = logs.filter(log => enabledLogTypes.includes(log.type));

  const logLevelOptions = [
    { value: 'info', label: 'Info', color: 'text-blue-500' },
    { value: 'error', label: 'Errors', color: 'text-red-500' },
    { value: 'success', label: 'Success', color: 'text-green-500' },
  ];

  const elfOptions = [
    { 
      value: 'pick', 
      label: 'Pick file', 
      icon: <Upload className="h-4 w-4" />,
      onClick: () => fileInputRef.current?.click() 
    },
    { 
      value: 'download', 
      label: 'Download from release', 
      icon: <Cloud className="h-4 w-4" />,
      onClick: () => onDownloadElf && onDownloadElf(),
      disabled: !onDownloadElf 
    },
  ];

  return (
    <div className="flex flex-col flex-1 min-h-0 rounded-lg bg-[var(--color-bg-tertiary)] p-4">
      <input
        type="file"
        ref={fileInputRef}
        onChange={handleFileSelect}
        accept=".elf"
        className="hidden"
      />
      <div className="flex items-center justify-between mb-2 flex-shrink-0">
        <div className="flex items-center gap-2 text-[var(--color-text-secondary)]">
          <TerminalIcon className="h-4 w-4" />
          <span className="text-sm font-medium">Terminal Monitor</span>
        </div>
        <div className="flex items-center gap-4">
          <Dropdown
            options={logLevelOptions}
            value={enabledLogTypes}
            onChange={(value) => setEnabledLogTypes(value as string[])}
            multiple={true}
            label="Log Levels"
            icon={<Filter className="h-4 w-4" />}
          />
          <button
            onClick={() => setAutoScroll(!autoScroll)}
            className={`p-1 transition-colors ${
              autoScroll ? 'text-blue-400 hover:text-blue-300' : 'text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)]'
            }`}
            title={autoScroll ? 'Disable autoscroll' : 'Enable autoscroll'}
          >
            <ArrowDownCircle className="h-4 w-4" />
          </button>
          
          {(onLoadElf || onDownloadElf) && (
             <Dropdown
               options={elfOptions.map(opt => ({
                 value: opt.value,
                 label: opt.label,
                 icon: opt.icon,
                 onClick: opt.onClick,
                 disabled: opt.disabled
               }))}
               value={[]}
               onChange={() => {}}
               label="Load debug symbols"
               icon={<FileCode className={cn("h-4 w-4", !disabled && (isElfLoaded ? "text-green-500" : "text-yellow-500"))} />}
               variant="icon"
               dropdownWidth="auto"
               disabled={disabled}
             />
          )}

          <button
            onClick={downloadLogs}
            className="p-1 text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] transition-colors"
            title="Download logs"
          >
            <Download className="h-4 w-4" />
          </button>
          <button
            onClick={clearLogs}
            className="p-1 text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] transition-colors"
            title="Clear logs"
          >
            <Trash2 className="h-4 w-4" />
          </button>
        </div>
      </div>

      <div className="flex-1 min-h-0 flex flex-col bg-[var(--color-bg-primary)] rounded overflow-hidden">
        {deviceInfo?.hasCoredump && onViewCoredump && onClearCoredump && 
         !['erasing', 'flashing', 'verifying'].includes(flashProgress?.status || '') && (
          <CoredumpBanner
            onView={onViewCoredump}
            onClear={onClearCoredump}
          />
        )}
        <div
          ref={terminalRef}
          className="flex-1 min-h-0 font-mono text-sm overflow-y-auto text-[var(--color-text-secondary)] scroll-smooth"
        >
          <div className="p-3 space-y-1">
            {filteredLogs.length > 0 ? (
              filteredLogs.map((log, index) => (
                <div
                  key={index}
                  className={`leading-relaxed ${
                    log.type === 'error'
                      ? 'text-red-400'
                      : log.type === 'success'
                      ? 'text-green-400'
                      : 'text-[var(--color-text-secondary)]'
                  }`}
                >
                  [{log.timestamp}]{' '}
                  {log.type === 'error'
                    ? '❌'
                    : log.type === 'success'
                    ? '✅'
                    : 'ℹ️'}{' '}
                  {log.message}
                </div>
              ))
            ) : (
              <div className="text-gray-500 italic">
                {disabled ? 'Connect a device to see terminal output...' : 'No logs to display'}
              </div>
            )}
          </div>
        </div>
      </div>

      <form onSubmit={handleSubmit} className="mt-3 flex gap-2">
        <input
          type="text"
          value={command}
          onChange={(e) => setCommand(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'ArrowUp') {
              e.preventDefault();
              if (commandBufferIndex.current > 0) {
                commandBufferIndex.current -= 1;
                setCommand(commandBuffer.current[commandBufferIndex.current] || '');
              }
            } else if (e.key === 'ArrowDown') {
              e.preventDefault();
              if (commandBufferIndex.current < commandBuffer.current.length - 1) {
                commandBufferIndex.current += 1;
                setCommand(commandBuffer.current[commandBufferIndex.current] || '');
              } else {
                commandBufferIndex.current = commandBuffer.current.length; // Reset to end
                setCommand('');
              }
            }
          }}
          disabled={disabled}
          placeholder={disabled ? 'Connect device to send commands...' : 'Enter command...'}
          className="flex-1 bg-[var(--color-bg-secondary)] rounded-lg px-3 py-2 text-sm text-[var(--color-text-primary)] placeholder-[var(--color-text-tertiary)] focus:outline-none focus:ring-2 focus:ring-blue-500 disabled:opacity-50 disabled:cursor-not-allowed"
        />
        <button
          type="submit"
          disabled={disabled || !command.trim()}
          className="flex items-center gap-2 rounded-lg bg-blue-600 px-4 py-2 text-sm font-medium text-white transition-colors hover:bg-blue-700 disabled:bg-[var(--color-bg-disabled)] disabled:text-[var(--color-text-disabled)] disabled:cursor-not-allowed"
        >
          <Send className="h-4 w-4" />
          Send
        </button>
      </form>
    </div>
  );
}