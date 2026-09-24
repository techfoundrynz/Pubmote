import React from 'react';
import { TerminalService } from '../services/terminal';
import { ESPService } from '../services/espService';
import { DeviceInfoData, FlashProgress } from '../types';

export type DeviceToolsContextType = {
  terminal: TerminalService;
  espService: ESPService;
  deviceInfo: DeviceInfoData;
  flashProgress: FlashProgress;
  setDeviceInfo: (info: DeviceInfoData) => void;
  setFlashProgress: (progress: FlashProgress) => void;
  sendTerminalCommand: (command: string) => Promise<void>;
  disconnect: () => void;
};

const DeviceToolsContext = React.createContext<DeviceToolsContextType | undefined>(undefined);

type DeviceToolsProviderProps = {
  children: React.ReactNode;
} & DeviceToolsContextType;

export const DeviceToolsProvider: React.FC<DeviceToolsProviderProps> = ({
  children,
  terminal,
  espService,
  deviceInfo,
  flashProgress,
  setDeviceInfo,
  setFlashProgress,
  sendTerminalCommand,
  disconnect,
}) => {
  const value = React.useMemo(
    () => ({
      terminal,
      espService,
      deviceInfo,
      flashProgress,
      setDeviceInfo,
      setFlashProgress,
      sendTerminalCommand,
      disconnect,
    }),
    [
      terminal,
      espService,
      deviceInfo,
      flashProgress,
      setDeviceInfo,
      setFlashProgress,
      sendTerminalCommand,
      disconnect,
    ],
  );

  return <DeviceToolsContext.Provider value={value}>{children}</DeviceToolsContext.Provider>;
};

export default DeviceToolsContext;
