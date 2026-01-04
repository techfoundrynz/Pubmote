import React from "react";
import { Eraser, Eye, EyeOff, RefreshCcw, Save } from "lucide-react";
import useDeviceTools from "../hooks/useDeviceTools";
import { LogListener } from "../services/espService";

type SettingsState = {
  wifi_ssid: string;
  wifi_password: string;
};

const DEFAULT_SETTINGS: SettingsState = {
  wifi_ssid: "",
  wifi_password: "",
};

const SettingsPage: React.FC<unknown> = () => {
  const { deviceInfo, flashProgress, sendTerminalCommand, espService } =
    useDeviceTools();
  const [settingsIsLoading, _setSettingsIsLoading] = React.useState(false);
  const settingsLoading = React.useRef<boolean>(false);

  const setSettingsLoading = React.useCallback((loading: boolean) => {
    settingsLoading.current = loading;
    _setSettingsIsLoading(loading);
  }, []);

  const [showWifiPassword, setshowWifiPassword] =
    React.useState<boolean>(false);
  const [settings, setSettings] = React.useState(DEFAULT_SETTINGS);
  const disabled =
    !deviceInfo.connected ||
    flashProgress.status !== "idle" ||
    settingsIsLoading;

  const retrieveSettings = React.useCallback(async (): Promise<
    Record<string, string>
  > => {
    const timeout = 5000;
    let wifi_ssid = "";
    let wifi_password = "";

    return new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        espService.removeLogListener(versionLogListener);
        reject(new Error("Timeout while waiting for settings response"));
      }, timeout);

      // Request firmware info
      espService.log("Fetching settings...");
      const versionLogListener: LogListener = (data) => {
        if (data.toLocaleLowerCase().startsWith("wifi_ssid:")) {
          // regex to match variant
          wifi_ssid = data.replace(/^wifi_ssid:\s*/i, "").trim();
        }

        if (data.toLowerCase().startsWith("wifi_password:")) {
          wifi_password = data.replace(/^wifi_password:\s*/i, "").trim();
        }

        if (data === "pubconsole>") {
          clearTimeout(timeoutId);
          espService.removeLogListener(versionLogListener);
          espService.log("Settings successfully loaded");
          resolve({ wifi_ssid, wifi_password });
        }

        return true; // Mark log as handled
      };
      espService.addLogListener(versionLogListener);
      espService.sendCommand("settings");
    });
  }, [espService]);

  const fetchSettings = React.useCallback(() => {
    if (!disabled && !settingsLoading.current) {
      setSettingsLoading(true);
      retrieveSettings()
        .then((settings) => {
          setSettings({
            wifi_ssid: settings.wifi_ssid || "",
            wifi_password: settings.wifi_password || "",
          });
          setSettingsLoading(false);
        })
        .catch((err) => {
          console.error("Failed to retrieve settings:", err);
          setSettingsLoading(false);
        });
    }
  }, [disabled, retrieveSettings, setSettingsLoading]);

  const resetSettings = React.useCallback(() => {
    setSettings(DEFAULT_SETTINGS);
  }, []);

  React.useEffect(() => {
    fetchSettings();

    if (!deviceInfo.connected) {
      resetSettings();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [deviceInfo.connected, flashProgress.status]);

  return (
    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-bold mb-4">Remote Settings</h2>
        <button
          onClick={fetchSettings}
          disabled={disabled}
          className="p-1 text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)] transition-colors"
          title="Reload settings"
        >
          <RefreshCcw className="h-5 w-5" />
        </button>
      </div>
      <div className="space-y-6">
        <div>
          <h3 className="font-medium mb-2">WiFi Network Name</h3>
          <input
            type="text"
            disabled={disabled}
            value={settings.wifi_ssid}
            autoComplete="wifi-ssid"
            onChange={(e) =>
              setSettings((prev) => ({ ...prev, wifi_ssid: e.target.value }))
            }
            placeholder="Enter WiFi network name"
            className="w-full px-3 py-2 bg-[var(--color-bg-tertiary)] border border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 focus:border-transparent text-[var(--color-text-primary)]"
          />
        </div>

        <div>
          <h3 className="font-medium mb-2">WiFi Password</h3>
          <div className="relative">
            <input
              type={showWifiPassword ? "text" : "password"}
              disabled={disabled}
              value={settings.wifi_password}
              autoComplete="wifi-key"
              onChange={(e) =>
                setSettings((prev) => ({
                  ...prev,
                  wifi_password: e.target.value,
                }))
              }
              placeholder="Enter WiFi password"
              className="w-full px-3 py-2 bg-[var(--color-bg-tertiary)] border border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 focus:border-transparent text-[var(--color-text-primary)] pr-16"
            />
            <div className="absolute inset-y-0 right-0 flex items-center pr-3">
              <button
                type="button"
                onClick={() => setshowWifiPassword((prev) => !prev)}
                className="text-gray-400 hover:text-[var(--color-text-primary)] text-sm"
              >
                {showWifiPassword ? (
                  <EyeOff className="h-5 w-5" />
                ) : (
                  <Eye className="h-5 w-5" />
                )}
              </button>
            </div>
          </div>
        </div>

        {/* <div className="flex items-center justify-between">
          <div>
            <h3 className="font-medium">Email Notifications</h3>
            <p className="text-sm text-gray-500">
              Receive email updates about your account
            </p>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input type="checkbox" className="sr-only peer" defaultChecked />
            <div className="w-11 h-6 bg-gray-200 peer-focus:outline-none peer-focus:ring-4 peer-focus:ring-blue-300 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-blue-600"></div>
          </label>
        </div>
        <div className="flex items-center justify-between">
          <div>
            <h3 className="font-medium">Dark Mode</h3>
            <p className="text-sm text-gray-500">Switch to dark theme</p>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input type="checkbox" className="sr-only peer" />
            <div className="w-11 h-6 bg-gray-200 peer-focus:outline-none peer-focus:ring-4 peer-focus:ring-blue-300 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-blue-600"></div>
          </label>
        </div> */}
        <div className="border-t pt-4 flex items-center justify-between gap-4">
          <button
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm font-medium text-white transition-colors bg-red-600 hover:bg-red-700 border border-red-500"
            disabled={disabled}
            onClick={() => {
              sendTerminalCommand("erase");
              resetSettings();
              // Refetch settings after a delay to allow erase to complete
              setTimeout(() => {
                fetchSettings();
              }, 2000);
            }}
          >
            <Eraser className="h-4 w-4" />
            Factory Reset
          </button>
          <button
            onClick={() => {
              let saveValuesString = "save_settings";
              Object.entries(settings).forEach(([key, value]) => {
                saveValuesString += ` ${key} "${value}"`;
              });

              sendTerminalCommand(saveValuesString);
            }}
            disabled={disabled}
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm font-medium text-white transition-colors bg-blue-600 hover:bg-blue-700 disabled:bg-[var(--color-bg-disabled)] disabled:text-[var(--color-text-disabled)] disabled:cursor-not-allowed"
          >
            <Save className="h-4 w-4" />
            Save
          </button>
        </div>
      </div>
    </div>
  );
};

export default SettingsPage;
