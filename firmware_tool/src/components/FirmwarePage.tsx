import React from "react";
import { FirmwareSelector } from "./FirmwareSelector";
import useDeviceTools from "../hooks/useDeviceTools";
import { FlashProgress } from "./FlashProgress";
import { FirmwareFiles } from "../types";
import { Usb, Sparkles } from "lucide-react";

const FirmwarePage: React.FC<{ onLoadElf?: (file: File) => void }> = ({ onLoadElf }) => {
  const { deviceInfo, espService, flashProgress, disconnect, setFlashProgress } = useDeviceTools();
  const [selectedFirmware, setSelectedFirmware] =
    React.useState<FirmwareFiles | null>(null);
  const [eraseFlash, setEraseFlash] = React.useState<boolean>(false);

  React.useEffect(() => {
    if (selectedFirmware?.elf) {
      // Only update the isElfLoaded state, handleLoadElf already calls espService.setElf
      if (onLoadElf) {
        onLoadElf(selectedFirmware.elf);
      }
    } else {
      espService.setElf(null);
    }
  }, [selectedFirmware, espService, onLoadElf]);

    
  const handleFlash = async () => {
    if (!selectedFirmware) return;

    try {
      if (eraseFlash) {
        setFlashProgress({ status: "erasing", progress: 20 });
      }

      const currentProgress = 20;

      await espService.flash(selectedFirmware, eraseFlash, (status) => {
        setFlashProgress({
          status: "flashing",
          progress: Math.round(
            currentProgress + status.progress * (100 - currentProgress)
          ),
        });
      });

      setFlashProgress({ status: "complete", progress: 100 });
      disconnect();
    } catch (error) {
      console.error("Flash error:", error);
      setFlashProgress({
        status: "error",
        progress: 0,
        error:
          error instanceof Error ? error.message : "Unknown error occurred",
      });
    }
  };

  const isFirstTimeInstall =
    deviceInfo.connected && deviceInfo.hasFirmware === false;

  return (
    <>
    {isFirstTimeInstall && (
      <div className="flex items-start gap-3 rounded-lg border border-blue-500/40 bg-blue-900/20 p-4 text-sm text-blue-100">
        <Sparkles className="h-5 w-5 flex-shrink-0 text-blue-400" />
        <div>
          <p className="font-medium">Fresh chip detected — first-time install</p>
          <p className="mt-1 text-blue-200/80">
            No firmware was found on this device. It's connected in bootloader
            mode and ready to flash. Select the firmware package for your
            hardware below, then click Flash Device.
          </p>
        </div>
      </div>
    )}
    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6">
        <FirmwareSelector
          onSelectFirmware={setSelectedFirmware}
          deviceInfo={deviceInfo}
        />
      </div>

    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6">
        <h2 className="mb-6 text-xl font-semibold">Flash Firmware</h2>

        <div className="flex items-center justify-between mb-6">
          <p>
            Finally, flash your selected firmware to the connected device.
            Enable erase flash if you want to clear the device's existing data
            before flashing the new firmware.
          </p>
        </div>

        <div className="space-y-4">
          <FlashProgress
            progress={flashProgress}
            isDeviceConnected={deviceInfo.connected}
            eraseFlash={eraseFlash}
            onEraseFlashChange={setEraseFlash}
            hasFirmwareFiles={!!selectedFirmware}
          />

          <button
            onClick={handleFlash}
            disabled={
              !selectedFirmware ||
              !deviceInfo.connected ||
              !["complete", "idle"].includes(flashProgress.status)
            }
            className="flex w-full items-center justify-center gap-2 rounded-lg bg-blue-600 px-4 py-2 font-medium text-white transition-colors hover:bg-blue-700 disabled:cursor-not-allowed disabled:bg-[var(--color-bg-disabled)] disabled:text-[var(--color-text-disabled)]"
          >
            <Usb className="h-5 w-5" />
            Flash Device
          </button>
        </div>
      </div>
    </>
  );
};

export default FirmwarePage;
