import React from "react";
import { X, FileText, Trash2, AlertTriangle } from "lucide-react";

interface CoredumpBannerProps {
  onView: () => void;
  onClear: () => void;
}

export const CoredumpBanner: React.FC<CoredumpBannerProps> = ({
  onView,
  onClear,
}) => {
  return (
    <div className="bg-[var(--color-danger)] border-b border-red-500/30 p-2 flex items-center justify-between animate-in fade-in slide-in-from-top-2 sticky top-0">
      <div className="flex items-center gap-2">
        <AlertTriangle className="h-4 w-4 text-white" />
        <span className="text-xs font-medium text-white">
          Crash Dump Detected
        </span>
      </div>
      <div className="flex items-center gap-2">
        <button
          onClick={onView}
          className="text-xs text-white hover:text-red-100 underline decoration-white/50 hover:decoration-white transition-colors"
        >
          Get Info
        </button>
        <button
          onClick={onClear}
          className="p-1 hover:bg-red-700 rounded text-white hover:text-red-100 transition-colors"
          title="Clear and Dismiss"
        >
          <Trash2 className="h-3 w-3" />
        </button>
      </div>
    </div>
  );
};
