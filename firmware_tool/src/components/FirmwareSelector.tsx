import React from "react";
import {
  Upload,
  Check,
  HardDrive,
  Table,
  Cpu,
  Archive,
  Tag,
  X,
} from "lucide-react";
import { DeviceInfoData, FirmwareFiles, ReleaseType } from "../types";
import { useFirmware } from "../hooks/useFirmware";
import { Dropdown } from "./ui/Dropdown";
import { Badge } from "./ui/Badge";
import { cn } from "../utils/cn";
import JSZip from "jszip";
import { useToast } from "../context/ToastContext";
import { Dialog } from "../components/ui/Dialog";
import { fetchWithCorsProxy } from "../utils/corsProxy";

interface FileUploadProps {
  label: string;
  icon: React.ReactNode;
  file: File | null;
  onChange: (file: File) => void;
  accept?: string;
}

function FileUpload({
  label,
  icon,
  file,
  onChange,
  accept = ".bin",
}: FileUploadProps) {
  const inputRef = React.useRef<HTMLInputElement>(null);
  const [isDragging, setIsDragging] = React.useState(false);
  const [isInvalid, setIsInvalid] = React.useState(false);

  const handleDragEnter = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(true);
  };

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(false);
    setIsInvalid(false);
  };

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(false);

    const droppedFile = e.dataTransfer.files[0];
    if (!droppedFile) return;

    const extension = accept.split(",")[0].replace("*", "");
    if (!droppedFile.name.endsWith(extension)) {
      setIsInvalid(true);
      setTimeout(() => setIsInvalid(false), 2000);
      return;
    }

    onChange(droppedFile);
  };

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const selectedFile = e.target.files?.[0];
    if (selectedFile) {
      onChange(selectedFile);
    }
  };

  return (
    <div
      onDragEnter={handleDragEnter}
      onDragLeave={handleDragLeave}
      onDragOver={handleDragOver}
      onDrop={handleDrop}
      className={cn(
        "flex flex-col items-center gap-4 rounded-lg p-6 transition-colors duration-200 border-2 border-dashed",
        file ? "border-gray-600" : "border-gray-700",
        isDragging &&
          !isInvalid &&
          "bg-blue-900/20 border-blue-500",
        isInvalid && "bg-red-900/20 border-red-500",
        "relative"
      )}
    >
      <input
        ref={inputRef}
        type="file"
        accept={accept}
        onChange={handleFileChange}
        className="hidden"
      />

      {isInvalid ? (
        <div className="absolute inset-0 flex flex-col items-center justify-center bg-[var(--color-bg-secondary)]/95 rounded-lg">
          <X className="h-8 w-8 text-red-500 mb-2" />
          <p className="text-sm text-red-400">Invalid file type</p>
        </div>
      ) : file ? (
        <Check className="h-8 w-8 text-blue-500" />
      ) : isDragging ? (
        <Upload className="h-8 w-8 text-blue-500" />
      ) : (
        <div className="h-8 w-8 text-gray-600">{icon}</div>
      )}

      <div className="text-center">
        {file ? (
          <>
            <p className="text-sm text-gray-200">{file.name}</p>
            <button
              onClick={() => inputRef.current?.click()}
              className="text-blue-500 hover:text-blue-400"
            >
              Choose different file
            </button>
          </>
        ) : (
          <>
            <p className="text-sm text-[var(--color-text-secondary)]">{label}</p>
            <button
              onClick={() => inputRef.current?.click()}
              className="text-blue-500 hover:text-blue-400"
            >
              Select file
            </button>
          </>
        )}
      </div>
    </div>
  );
}

export const FirmwareSelector: React.FC<FirmwareSelectorProps> = (props) => {
  const { onSelectFirmware, deviceInfo } = props;
  const { versions, loading, error: fetchError } = useFirmware();
  const { toast } = useToast();
  const [isDownloading, setIsDownloading] = React.useState(false);
  const [errorDialog, setErrorDialog] = React.useState<{isOpen: boolean, title: string, message: string} | null>(null);
  const [files, setFiles] = React.useState<
    FirmwareFiles & { zip: File | null }
  >({
    bootloader: null,
    partitionTable: null,
    application: null,
    elf: null,
    zip: null,
  });
  const [uploadMode, setUploadMode] = React.useState<"individual" | "package">(
    "package"
  );

  const extractFirmwareFiles = async (file: File): Promise<FirmwareFiles> => {
    try {
      const zip = new JSZip();
      const contents = await zip.loadAsync(file);

      const bootloaderFile = contents.file("bootloader.bin");
      const partitionsFile = contents.file("partitions.bin");
      const firmwareFile = contents.file("firmware.bin");
      const elfFile = contents.file("firmware.elf");

      if (!bootloaderFile || !partitionsFile || !firmwareFile) {
        throw new Error("Invalid firmware package - missing required files");
      }

      const promises: Promise<Blob>[] = [
        bootloaderFile.async("blob"),
        partitionsFile.async("blob"),
        firmwareFile.async("blob"),
      ];

      if (elfFile) {
        promises.push(elfFile.async("blob"));
      }

      const blobs = await Promise.all(promises);
      
      const [bootloaderBlob, partitionsBlob, firmwareBlob] = blobs;
      const elfBlob = elfFile ? blobs[3] : null;

      return {
        bootloader: new File([bootloaderBlob], "bootloader.bin", {
          type: "application/octet-stream",
        }),
        partitionTable: new File([partitionsBlob], "partitions.bin", {
          type: "application/octet-stream",
        }),
        application: new File([firmwareBlob], "firmware.bin", {
          type: "application/octet-stream",
        }),
        elf: elfBlob ? new File([elfBlob], "firmware.elf", {
           type: "application/octet-stream" 
        }) : null,
      };
    } catch (error) {
      console.error("Failed to extract firmware files:", error);
      throw new Error("Invalid firmware package");
    }
  };

  const handleIndividualFileChange =
    (type: keyof FirmwareFiles) => (file: File) => {
      const newFiles = { ...files, [type]: file };
      setFiles(newFiles);
      onSelectFirmware(newFiles);
    };

  const handlePackageFileChange = async (file: File) => {
    try {
      const extractedFiles = await extractFirmwareFiles(file);
      setFiles({
        bootloader: extractedFiles.bootloader,
        partitionTable: extractedFiles.partitionTable,
        application: extractedFiles.application,
        elf: extractedFiles.elf,
        zip: file,
      });
      onSelectFirmware(extractedFiles);
      toast.success(`Loaded firmware package: ${file.name}`);
    } catch (error) {
      setFiles({
        bootloader: null,
        partitionTable: null,
        application: null,
        elf: null,
        zip: null,
      });
      onSelectFirmware(null);
      throw error;
    }
  };

  const versionOptions = versions.flatMap((version) =>
    version.variants
      .filter((v) => deviceInfo?.hardware ? v.variant === deviceInfo.hardware : true)
      .map((variant) => ({
        value: variant.zipUrl,
        tooltip: `Version ${version.version} (${variant.variant}) - ${new Date(
          version.date
        ).toLocaleDateString()}`,
        label: (
          <div className="flex items-center justify-between w-full">
            <div className="flex-1 truncate">
              <span className="mr-2">{version.version}</span>
              <span className="text-gray-400">{variant.variant}</span>
              <span className="ml-2 text-gray-400 text-xs">
                {new Date(version.date).toLocaleDateString()}
              </span>
            </div>
            {version.releaseType === ReleaseType.Release && (
              <Badge variant="default" className="ml-2 flex-shrink-0">
                Stable
              </Badge>
            )}
            {version.releaseType === ReleaseType.Prerelease && (
              <Badge variant="warning" className="ml-2 flex-shrink-0">
                Prerelease
              </Badge>
            )}
            {version.releaseType === ReleaseType.Nightly && (
              <Badge variant="destructive" className="ml-2 flex-shrink-0">
                Nightly
              </Badge>
            )}
          </div>
        ),
      }))
  );

  const handleVersionSelect = async (url: string) => {
    let toastId: string | undefined;
    try {
      setIsDownloading(true);
      toastId = toast.info("Downloading firmware package...", 0);
      
      const response = await fetchWithCorsProxy(url);
      if (!response.ok) throw new Error(`Download failed: ${response.statusText}`);
      
      const blob = await response.blob();
      // Extract filename from URL or default
      const filename = url.split('/').pop() || "firmware.zip";
      const file = new File([blob], filename, { type: "application/zip" });
      
      await handlePackageFileChange(file);
    } catch (err) {
      console.error("Failed to download firmware via CORS proxy, falling back to direct download:", err);
      setErrorDialog({
          isOpen: true,
          title: "Download Failed",
          message: "Auto-download failed. Opening browser to download manually."
      });
      // Fallback to direct download
      window.open(url, "_blank");
    } finally {
      if (toastId) toast.dismiss(toastId);
      setIsDownloading(false);
    }
  };

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold">Select Firmware Files</h2>
        <Dropdown
          options={versionOptions}
          value=""
          onChange={(value) => handleVersionSelect(value as string)}
          label={
            loading
              ? "Loading..."
              : isDownloading
              ? "Downloading..."
              : versions.length === 0
              ? "No releases available"
              : "Download Release"
          }
          icon={<Tag className="h-4 w-4" />}
          disabled={loading || versions.length === 0 || isDownloading}
          width="fixed"
          dropdownWidth={400}
        />
      </div>

      <div className="flex items-center justify-between mb-6">
        <p>
          Upload your desired firmware files here. You can either upload a complete firmware package (ZIP file) or individual files.
        </p>
      </div>

      <div className="flex justify-center">
        <div className="flex rounded-lg bg-[var(--color-bg-tertiary)] p-1">
          <button
            onClick={() => setUploadMode("package")}
            className={cn(
              "px-3 py-1.5 text-sm font-medium rounded-md transition-colors",
              uploadMode === "package"
                ? "bg-blue-600 text-white"
                : "text-gray-400 hover:text-[var(--color-text-primary)]"
            )}
          >
            Package
          </button>
          <button
            onClick={() => setUploadMode("individual")}
            className={cn(
              "px-3 py-1.5 text-sm font-medium rounded-md transition-colors",
              uploadMode === "individual"
                ? "bg-blue-600 text-white"
                : "text-gray-400 hover:text-[var(--color-text-primary)]"
            )}
          >
            Individual Files
          </button>
        </div>
      </div>

      {fetchError && (
        <div className="p-3 rounded-lg bg-red-900/50 text-red-200">
          {fetchError}
        </div>
      )}

      <Dialog 
        isOpen={!!errorDialog?.isOpen} 
        onClose={() => setErrorDialog(null)}
        title={errorDialog?.title || ""}
        message={errorDialog?.message || ""}
      />

      {uploadMode === "individual" ? (
        <div className="grid gap-4 md:grid-cols-3">
          <FileUpload
            label="Select Bootloader"
            icon={<HardDrive className="h-full w-full" />}
            file={files.bootloader}
            onChange={handleIndividualFileChange("bootloader")}
          />

          <FileUpload
            label="Select Partition Table"
            icon={<Table className="h-full w-full" />}
            file={files.partitionTable}
            onChange={handleIndividualFileChange("partitionTable")}
          />

          <FileUpload
            label="Select Application"
            icon={<Cpu className="h-full w-full" />}
            file={files.application}
            onChange={handleIndividualFileChange("application")}
          />
        </div>
      ) : (
        <FileUpload
          label="Select or drop firmware package"
          icon={<Archive className="h-full w-full" />}
          file={files.zip}
          onChange={handlePackageFileChange}
          accept=".zip"
        />
      )}
    </div>
  );
};

interface FirmwareSelectorProps {
  onSelectFirmware: (files: FirmwareFiles | null) => void;
  deviceInfo?: DeviceInfoData;
}
