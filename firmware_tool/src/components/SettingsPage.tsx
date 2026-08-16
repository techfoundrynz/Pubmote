import React from "react";
import {
  AlertTriangle,
  Eraser,
  Eye,
  EyeOff,
  RefreshCcw,
  Save,
} from "lucide-react";
import useDeviceTools from "../hooks/useDeviceTools";
import { useToast } from "../context/ToastContext";
import { LogListener } from "../services/espService";
import { Dropdown } from "./ui/Dropdown";

type SettingsState = {
  wifi_ssid: string;
  wifi_password: string;
  js_x_gpio: string;
  js_y_gpio: string;
  btn1_gpio: string;
  btn1_level: string;
};

// Reported by the device, so the tool needs no per-chip pin tables
type PinInfo = {
  available: number[];
  adcCapable: number[];
  buttonCapable: number[];
  warning: string;
  // Without this an old build looks identical to "every input unused"
  supported: boolean;
};

const PIN_DISABLED = "-1";

const DEFAULT_SETTINGS: SettingsState = {
  wifi_ssid: "",
  wifi_password: "",
  js_x_gpio: PIN_DISABLED,
  js_y_gpio: PIN_DISABLED,
  btn1_gpio: PIN_DISABLED,
  btn1_level: "0",
};

const DEFAULT_PIN_INFO: PinInfo = {
  available: [],
  adcCapable: [],
  buttonCapable: [],
  warning: "",
  supported: false,
};

const PIN_KEYS = [
  "js_x_gpio",
  "js_y_gpio",
  "btn1_gpio",
  "btn1_level",
] as const satisfies readonly (keyof SettingsState)[];

// Sized to match the Dropdown control
const INPUT_CLASSES =
  "w-full rounded-lg border border-gray-600 bg-[var(--color-bg-tertiary)] px-3 py-1.5 text-sm text-[var(--color-text-primary)] focus:border-transparent focus:ring-2 focus:ring-blue-500 disabled:cursor-not-allowed disabled:text-[var(--color-text-disabled)]";

const parsePinList = (value: string | undefined): number[] => {
  if (!value) return [];
  return value
    .split(",")
    .map((entry) => Number.parseInt(entry.trim(), 10))
    .filter((pin) => Number.isInteger(pin));
};

const SettingsPage: React.FC<unknown> = () => {
  const { deviceInfo, flashProgress, sendTerminalCommand, espService } =
    useDeviceTools();
  const { toast } = useToast();
  const [settingsIsLoading, _setSettingsIsLoading] = React.useState(false);
  const settingsLoading = React.useRef<boolean>(false);

  const setSettingsLoading = React.useCallback((loading: boolean) => {
    settingsLoading.current = loading;
    _setSettingsIsLoading(loading);
  }, []);

  const [showWifiPassword, setshowWifiPassword] =
    React.useState<boolean>(false);
  const [settings, setSettings] = React.useState(DEFAULT_SETTINGS);
  // Used to send only what changed, so saving WiFi doesn't re-apply pins
  const [loadedSettings, setLoadedSettings] = React.useState(DEFAULT_SETTINGS);
  const [pinInfo, setPinInfo] = React.useState(DEFAULT_PIN_INFO);
  const disabled =
    !deviceInfo.connected ||
    flashProgress.status !== "idle" ||
    settingsIsLoading;

  const retrieveSettings = React.useCallback(async (): Promise<
    Record<string, string>
  > => {
    const timeout = 5000;
    const values: Record<string, string> = {};

    return new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        espService.removeLogListener(versionLogListener);
        reject(new Error("Timeout while waiting for settings response"));
      }, timeout);

      // Request firmware info
      espService.log("Fetching settings...");
      const versionLogListener: LogListener = (data) => {
        // "key: value" replies; log lines can't match (space before the colon)
        const match = data.match(/^([a-z0-9_]+):\s*(.*)$/i);
        if (match) {
          values[match[1].toLowerCase()] = match[2].trim();
        }

        if (data === "pubconsole>") {
          clearTimeout(timeoutId);
          espService.removeLogListener(versionLogListener);
          espService.log("Settings successfully loaded");
          resolve(values);
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
        .then((values) => {
          const loaded: SettingsState = {
            wifi_ssid: values.wifi_ssid || "",
            wifi_password: values.wifi_password || "",
            js_x_gpio: values.js_x_gpio || PIN_DISABLED,
            js_y_gpio: values.js_y_gpio || PIN_DISABLED,
            btn1_gpio: values.btn1_gpio || PIN_DISABLED,
            btn1_level: values.btn1_level || "0",
          };
          setSettings(loaded);
          setLoadedSettings(loaded);
          setPinInfo({
            available: parsePinList(values.pins_available),
            adcCapable: parsePinList(values.pins_adc_capable),
            buttonCapable: parsePinList(values.pins_button_capable),
            warning: values.pins_warning || "",
            // Require the keys we read: a renamed key would otherwise look
            // like an unused input
            supported:
              values.pins_available !== undefined &&
              values.btn1_gpio !== undefined &&
              values.js_x_gpio !== undefined &&
              values.js_y_gpio !== undefined,
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
    setLoadedSettings(DEFAULT_SETTINGS);
    setPinInfo(DEFAULT_PIN_INFO);
  }, []);

  React.useEffect(() => {
    fetchSettings();

    if (!deviceInfo.connected) {
      resetSettings();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [deviceInfo.connected, flashProgress.status]);

  // Surface the firmware's verdict on a remap, plus any caveats
  const watchPinResult = React.useCallback(() => {
    const listener: LogListener = (data) => {
      if (data.startsWith("pins_error:")) {
        toast.error(data.replace(/^pins_error:\s*/, ""), 8000);
      } else if (data.startsWith("pins_applied:")) {
        toast.success("Input pins applied", 4000);
      } else if (data.startsWith("pins_warning:")) {
        toast.warning(data.replace(/^pins_warning:\s*/, ""), 10000);
      }
      return false; // Leave the line in the terminal too
    };

    espService.addLogListener(listener);
    setTimeout(() => espService.removeLogListener(listener), 5000);
  }, [espService, toast]);

  const pinsChanged = PIN_KEYS.some(
    (key) => settings[key] !== loadedSettings[key]
  );

  const handleSave = React.useCallback(() => {
    let saveValuesString = "save_settings";
    const keysToSend: (keyof SettingsState)[] = [
      "wifi_ssid",
      "wifi_password",
      // Only remap when something changed - applying rebuilds the inputs
      ...PIN_KEYS.filter((key) => settings[key] !== loadedSettings[key]),
    ];

    keysToSend.forEach((key) => {
      saveValuesString += ` ${key} "${settings[key]}"`;
    });

    if (pinsChanged) {
      watchPinResult();
    }

    sendTerminalCommand(saveValuesString);
    setLoadedSettings(settings);

    // Pull back the applied state - a remap can be rejected or reset a
    // calibration
    setTimeout(() => fetchSettings(), 1500);
  }, [
    fetchSettings,
    loadedSettings,
    pinsChanged,
    sendTerminalCommand,
    settings,
    watchPinResult,
  ]);

  const pinOptions = React.useCallback(
    (allowed: number[], current: string) => {
      const options = [
        { value: PIN_DISABLED, label: "Not used" },
        ...allowed.map((pin) => ({
          value: String(pin),
          label: `GPIO ${pin}`,
        })),
      ];

      // Keep the device's own value selectable even if not in the allowed list
      if (
        current !== PIN_DISABLED &&
        !options.some((option) => option.value === current)
      ) {
        options.push({ value: current, label: `GPIO ${current} (current)` });
      }

      return options;
    },
    []
  );

  const pinLabel = (value: string) =>
    value === PIN_DISABLED ? "Not used" : `GPIO ${value}`;

  const noButton = settings.btn1_gpio === PIN_DISABLED;
  // Firmware that can't report its pins won't accept them either
  const pinsDisabled = disabled || !pinInfo.supported;

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
          <h3 className="font-medium">Wi-Fi</h3>
          <p className="text-sm text-[var(--color-text-secondary)] mb-3">
            Used for over-the-air firmware updates. Leave blank to keep the
            remote offline.
          </p>

          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <span className="block text-sm mb-1">Network name</span>
              <input
                type="text"
                disabled={disabled}
                value={settings.wifi_ssid}
                autoComplete="wifi-ssid"
                onChange={(e) =>
                  setSettings((prev) => ({
                    ...prev,
                    wifi_ssid: e.target.value,
                  }))
                }
                placeholder="Enter WiFi network name"
                className={INPUT_CLASSES}
              />
            </div>

            <div>
              <span className="block text-sm mb-1">Password</span>
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
                  className={`${INPUT_CLASSES} pr-10`}
                />
                <button
                  type="button"
                  onClick={() => setshowWifiPassword((prev) => !prev)}
                  className="absolute inset-y-0 right-0 flex items-center pr-3 text-gray-400 hover:text-[var(--color-text-primary)]"
                >
                  {showWifiPassword ? (
                    <EyeOff className="h-4 w-4" />
                  ) : (
                    <Eye className="h-4 w-4" />
                  )}
                </button>
              </div>
            </div>
          </div>
        </div>

        <div className="border-t border-gray-700 pt-4">
          <h3 className="font-medium">Input Pins</h3>
          <p className="text-sm text-[var(--color-text-secondary)] mb-3">
            Remap the joystick axes and the primary button. Changes apply
            immediately - no reboot or reflash. Axes must sit on an analog
            capable pin and the button on an RTC pin so it can still wake the
            remote; pins already used by this board are not listed.
          </p>

          {deviceInfo.connected && !settingsIsLoading && !pinInfo.supported && (
            <div className="mb-3 flex items-start gap-2 rounded-lg border border-yellow-600/50 bg-yellow-500/10 p-3 text-sm text-yellow-200">
              <AlertTriangle className="mt-0.5 h-4 w-4 flex-shrink-0" />
              <span>
                This firmware doesn't report the input pins this tool expects -
                flash a newer build to remap them. The values below are not what
                the remote is using.
              </span>
            </div>
          )}

          <div className="grid gap-3 sm:grid-cols-2">
            <div>
              <span className="block text-sm mb-1">Joystick X axis</span>
              <Dropdown
                label={pinLabel(settings.js_x_gpio)}
                disabled={pinsDisabled}
                value={settings.js_x_gpio}
                options={pinOptions(pinInfo.adcCapable, settings.js_x_gpio)}
                onChange={(value) =>
                  setSettings((prev) => ({
                    ...prev,
                    js_x_gpio: value as string,
                  }))
                }
              />
            </div>

            <div>
              <span className="block text-sm mb-1">Joystick Y axis</span>
              <Dropdown
                label={pinLabel(settings.js_y_gpio)}
                disabled={pinsDisabled}
                value={settings.js_y_gpio}
                options={pinOptions(pinInfo.adcCapable, settings.js_y_gpio)}
                onChange={(value) =>
                  setSettings((prev) => ({
                    ...prev,
                    js_y_gpio: value as string,
                  }))
                }
              />
            </div>

            <div>
              <span className="block text-sm mb-1">Primary button</span>
              <Dropdown
                label={pinLabel(settings.btn1_gpio)}
                disabled={pinsDisabled}
                value={settings.btn1_gpio}
                options={pinOptions(pinInfo.buttonCapable, settings.btn1_gpio)}
                onChange={(value) =>
                  setSettings((prev) => ({
                    ...prev,
                    btn1_gpio: value as string,
                  }))
                }
              />
            </div>

            <div>
              <span className="block text-sm mb-1">Button active level</span>
              <Dropdown
                label={
                  settings.btn1_level === "1"
                    ? "Active high"
                    : "Active low"
                }
                disabled={pinsDisabled || noButton}
                value={settings.btn1_level}
                options={[
                  { value: "0", label: "Active low" },
                  { value: "1", label: "Active high" },
                ]}
                onChange={(value) =>
                  setSettings((prev) => ({
                    ...prev,
                    btn1_level: value as string,
                  }))
                }
              />
            </div>
          </div>

          {noButton && (
            <div className="mt-3 flex items-start gap-2 rounded-lg border border-yellow-600/50 bg-yellow-500/10 p-3 text-sm text-yellow-200">
              <AlertTriangle className="mt-0.5 h-4 w-4 flex-shrink-0" />
              <span>
                With no primary button the remote can't be powered off or woken
                from deep sleep by button - it will need a reset to come back.
              </span>
            </div>
          )}

          {pinInfo.warning && (
            <div className="mt-3 flex items-start gap-2 rounded-lg border border-yellow-600/50 bg-yellow-500/10 p-3 text-sm text-yellow-200">
              <AlertTriangle className="mt-0.5 h-4 w-4 flex-shrink-0" />
              <span>{pinInfo.warning}</span>
            </div>
          )}

          {pinsChanged && (
            <p className="mt-3 text-sm text-[var(--color-text-secondary)]">
              Moving an axis to a different pin clears that axis' calibration -
              recalibrate the joystick on the remote after saving.
            </p>
          )}
        </div>

        <div className="border-t pt-4 flex items-center justify-between gap-4">
          <button
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm font-medium text-white transition-colors bg-[var(--color-danger)] hover:bg-red-700 border border-red-500"
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
            onClick={handleSave}
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
