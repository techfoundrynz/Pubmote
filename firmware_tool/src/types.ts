export interface FirmwareVariant {
  zipUrl: string;
  date: string;
  variant: string;
}

export interface DeviceInfoData {
  connected: boolean;
  chipId?: string;
  macAddress?: string;
  version?: string;
  variant?: string;
  hardware?: string;
  hasCoredump?: boolean;
  hasFirmware?: boolean;
}

export interface FlashProgress {
  status: 'idle' | 'connecting' | 'erasing' | 'flashing' | 'verifying' | 'complete' | 'error';
  progress: number;
  error?: string;
}

export enum ReleaseType {
  Release = 'release',
  Prerelease = 'prerelease',
  Nightly = 'nightly',
}

export interface FirmwareVersion {
  version: string;
  date: string;
  variants: FirmwareVariant[];
  releaseType: ReleaseType;
}

export interface FirmwareFiles {
  bootloader: File | null;
  partitionTable: File | null;
  application: File | null;
  elf: File | null;
  littlefs?: File | null;
}

export interface CommandInfo {
  name: string;
  help: string;
  hint?: string;
}

export interface GitHubRelease {
  name: string;
  tag_name: string;
  published_at: string;
  prerelease: boolean;
  assets: Array<{
    name: string;
    browser_download_url: string;
    url: string;
  }>;
}
