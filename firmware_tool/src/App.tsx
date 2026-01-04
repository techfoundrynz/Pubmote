import React, { useRef, useState } from "react";
import Header from "./components/Header";
import { ESPService } from "./services/espService";
import { TerminalService } from "./services/terminal";
import { DeviceInfoData, FlashProgress as FlashProgressType } from "./types";
import SettingsPage from "./components/SettingsPage";
import FirmwarePage from "./components/FirmwarePage";
import DeviceInfoPage from "./components/DeviceInfoPage";
import { DeviceToolsProvider } from "./context/DeviceToolsContext";
import { DeviceInfo } from "./components/DeviceInfo";
import { ToastProvider, useToast } from "./context/ToastContext";
import { Dialog } from "./components/ui/Dialog";

// Helper function to compare semantic versions
const compareVersions = (v1: string, v2: string): number => {
  const parts1 = v1.replace(/^v/, '').split('.').map(Number);
  const parts2 = v2.replace(/^v/, '').split('.').map(Number);
  
  for (let i = 0; i < Math.max(parts1.length, parts2.length); i++) {
    const part1 = parts1[i] || 0;
    const part2 = parts2[i] || 0;
    
    if (part1 > part2) return 1;
    if (part1 < part2) return -1;
  }
  
  return 0;
};

const AppContent = () => {
  const terminal = useRef<TerminalService>(new TerminalService()).current;
  const espService = useRef<ESPService>(new ESPService(terminal)).current;
  const { toast } = useToast();
  const [isConnecting, setIsConnecting] = useState(false);
  const [activeTab, setActiveTab] = useState(0);
  const [flashProgress, setFlashProgress] = useState<FlashProgressType>({
    status: "idle",
    progress: 0,
  });
  const [deviceInfo, setDeviceInfo] = useState<DeviceInfoData>({
    connected: false,
  });
  const [isElfLoaded, setIsElfLoaded] = useState(false);
  const [errorDialog, setErrorDialog] = useState<{isOpen: boolean, title: string, message: string} | null>(null);
  const [latestVersion, setLatestVersion] = useState<string | null>(null);

  const handleSendTerminalCommand = React.useCallback(
    async (command: string) => {
      try {
        await espService.sendCommand(command);
      } catch (error) {
        console.error("Command error:", error);
      }
    },
    [espService]
  );

  const checkForUpdates = async () => {
    try {
      const response = await fetch('https://api.github.com/repos/techfoundrynz/pubmote/releases');
      if (!response.ok) return;
      
      const releases = await response.json();
      if (releases && releases.length > 0) {
        // Filter out nightly builds (releases with "nightly" in tag_name)
        const nonNightlyReleases = releases.filter((release: any) => 
          !release.tag_name.toLowerCase().includes('nightly')
        );
        
        if (nonNightlyReleases.length === 0) return;
        
        // Find the latest version by comparing all non-nightly releases
        let latestRelease = nonNightlyReleases[0];
        for (const release of nonNightlyReleases) {
          const currentVersion = release.tag_name.replace(/^v/, '');
          const latestVersionStr = latestRelease.tag_name.replace(/^v/, '');
          
          if (compareVersions(currentVersion, latestVersionStr) > 0) {
            latestRelease = release;
          }
        }
        
        // Extract version from tag_name (e.g., "v1.2.3" -> "1.2.3")
        const version = latestRelease.tag_name.replace(/^v/, '');
        setLatestVersion(version);
      }
    } catch (error) {
      console.error("Failed to check for updates:", error);
    }
  };

  const handleConnect = async () => {
    setIsConnecting(true);
    try {
      setDeviceInfo({ connected: false });
      const info = await espService.connect();
      setDeviceInfo({
        ...info,
        connected: true,
      });
      toast.success("Device connected");
      // Check for updates after successful connection
      checkForUpdates();
    } catch (error) {
      console.error("Connection error:", error);
      setDeviceInfo({ connected: false });
      const errorMessage = error instanceof Error ? error.message : "Unknown error occurred";
      toast.error(`Connection failed: ${errorMessage}`);
    } finally {
      setIsConnecting(false);
    }
  };

  const handleDisconnect = () => {
    espService.disconnect();
    setDeviceInfo({ connected: false });
    toast.info("Device disconnected");
  };

  const handleViewCoredump = async () => {
    try {
      await espService.sendCommand("coredump_print");
    } catch (e) {
      console.error(e);
    }
  };

  const handleClearCoredump = async () => {
    try {
      await espService.sendCommand("coredump_erase");
      setDeviceInfo((prev) => ({ ...prev, hasCoredump: false }));
    } catch (e) {
      console.error(e);
    }
  };

  const handleLoadElf = React.useCallback(async (file: File) => {
    try {
      await espService.setElf(file);
      setIsElfLoaded(true);
      terminal.log(`Loaded debug symbols from ${file.name}`, "success");
    } catch (error) {
      console.error("Failed to load ELF:", error);
      terminal.log("Failed to load debug symbols", "error");
      setIsElfLoaded(false);
    }
  }, [espService, terminal]);

  const handleDownloadElf = async () => {
    if (!deviceInfo.version || !deviceInfo.variant) {
      toast.error("Cannot download symbols: missing firmware version/variant info");
      return;
    }

    try {
      toast.info("Fetching release data...");
      const response = await fetch('https://api.github.com/repos/techfoundrynz/pubmote/releases');
      if (!response.ok) throw new Error(`GitHub API Error: ${response.statusText}`);
      
      const releases = await response.json();
      const version = deviceInfo.version;
      const variant = deviceInfo.variant;

      // 1. Try to find precise tag match
      let release = releases.find((r: any) => r.tag_name === version || r.name === version);
      
      if (!release) {
         release = releases.find((r: any) => r.tag_name === `v${version}` || r.name === `v${version}`);
      }

      if (!release) {
        release = releases[0];
        toast.warning(`Release '${version}' not found. Using latest release '${release.tag_name}'.`);
      }

      // 3. Find ELF asset
      const elfAsset = release.assets.find((asset: any) => 
        asset.name.endsWith('.elf') && asset.name.includes(variant)
      );

      if (!elfAsset) {
        throw new Error(`No ELF file found for variant '${variant}' in release '${release.tag_name}'`);
      }

      // 4. Download via Proxy
      const originalUrl = elfAsset.browser_download_url;
      const proxyUrl = `https://api.allorigins.win/raw?url=${encodeURIComponent(originalUrl)}`;

      try {
        const toastId = toast.info(`Downloading ${elfAsset.name}...`, 0);
        
        try {
          const fileRes = await fetch(proxyUrl);
          if (!fileRes.ok) throw new Error(`Download failed: ${fileRes.statusText}`);

          const blob = await fileRes.blob();
          const file = new File([blob], elfAsset.name, { type: "application/octet-stream" });

          handleLoadElf(file);
        } finally {
          toast.dismiss(toastId);
        }
      } catch (downloadError) {
        console.warn("Proxy download failed, falling back to direct download:", downloadError);
        setErrorDialog({
            isOpen: true,
            title: "Download Failed",
            message: "Auto-download failed. Opening browser to download manually."
        });
        window.open(originalUrl, '_blank');
      }

    } catch (error) {
       console.error("Failed to fetch release info:", error);
       setErrorDialog({
           isOpen: true,
           title: "Error Checking Releases",
           message: error instanceof Error ? error.message : "Unknown error occurred"
       });
    }
  };

  const tabs = [
    {
      label: "Firmware",
      content: <FirmwarePage onLoadElf={handleLoadElf} />,
    },
    {
      label: "Settings",
      content: <SettingsPage />,
    },
    {
      label: "Float Accessories",
      content: <DeviceInfoPage />,
    },
  ];

  return (
    <div className="h-screen flex flex-col bg-[var(--color-bg-primary)] text-[var(--color-text-primary)] overflow-hidden">
      <Header 
        isConnected={deviceInfo.connected}
        isConnecting={isConnecting}
        onConnect={handleConnect}
        onDisconnect={handleDisconnect}
      />
      <Dialog 
        isOpen={!!errorDialog?.isOpen} 
        onClose={() => setErrorDialog(null)}
        title={errorDialog?.title || ""}
        message={errorDialog?.message || ""}
      />
      <main className="flex-1 min-h-0 px-4 pt-6 pb-8 max-w-screen-2xl mx-auto w-full">
        <DeviceToolsProvider
          terminal={terminal}
          espService={espService}
          deviceInfo={deviceInfo}
          setDeviceInfo={setDeviceInfo}
          flashProgress={flashProgress}
          setFlashProgress={setFlashProgress}
          sendTerminalCommand={handleSendTerminalCommand}
          disconnect={handleDisconnect}
        >
          <div className="grid grid-cols-1 lg:grid-cols-12 gap-6 h-full">
            {/* Sidebar */}
            <div className="lg:col-span-6 flex flex-col min-h-0">
              <DeviceInfo
                deviceInfo={deviceInfo}
                onConnect={handleConnect}
                onDisconnect={handleDisconnect}
                onSendCommand={handleSendTerminalCommand}
                terminal={terminal}
                onViewCoredump={handleViewCoredump}
                onClearCoredump={handleClearCoredump}
                onLoadElf={handleLoadElf}
                onDownloadElf={handleDownloadElf}
                isElfLoaded={isElfLoaded}
                updateAvailable={!!(latestVersion && deviceInfo.version && compareVersions(latestVersion, deviceInfo.version) > 0)}
              />
            </div>

            {/* Main Content */}
            <div className="lg:col-span-6 flex flex-col min-h-0">
              <nav className="flex space-x-8 mb-6 border-b border-gray-800 flex-shrink-0" aria-label="Tabs">
                {tabs.map((tab, index) => (
                  <button
                    key={index}
                    onClick={() => setActiveTab(index)}
                    className={`py-2 px-1 border-b-2 font-medium text-sm transition-colors duration-200 -mb-[2px] ${
                      activeTab === index
                        ? "border-blue-500 text-blue-500"
                        : "border-transparent text-gray-400 hover:text-gray-300 hover:border-gray-700"
                    }`}
                    aria-selected={activeTab === index}
                  >
                    {tab.label}
                  </button>
                ))}
              </nav>
              
              <div className="min-h-0 overflow-y-auto space-y-6">
                {tabs[activeTab].content}
              </div>
            </div>
          </div>
        </DeviceToolsProvider>
      </main>
    </div>
  );
};

const App = () => {
    return (
        <ToastProvider>
            <AppContent />
        </ToastProvider>
    )
}

export default App;
