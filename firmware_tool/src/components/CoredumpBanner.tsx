import React from "react";
import { X, FileText, Trash2, AlertTriangle } from "lucide-react";

interface CoredumpBannerProps {
  onView: () => void;
  onClear: () => void;
  compact?: boolean;
}

export const CoredumpBanner: React.FC<CoredumpBannerProps> = ({
  onView,
  onClear,
  compact = false,
}) => {
  if (compact) {
    return (
      <div className="bg-red-900 border-b border-red-500/30 p-2 flex items-center justify-between animate-in fade-in slide-in-from-top-2 sticky top-0">
        <div className="flex items-center gap-2">
          <AlertTriangle className="h-4 w-4 text-red-400" />
          <span className="text-xs font-medium text-red-100">
            Crash Dump Detected
          </span>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={onView}
            className="text-xs text-red-200 hover:text-white underline decoration-red-400/50 hover:decoration-red-400 transition-colors"
          >
            View Stacktrace
          </button>
          <button
            onClick={onClear}
            className="p-1 hover:bg-red-800 rounded text-red-300 hover:text-white transition-colors"
            title="Clear and Dismiss"
          >
            <Trash2 className="h-3 w-3" />
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="bg-red-900/30 border-l-4 border-red-500 p-4 mb-4 rounded-r shadow-lg animate-in fade-in slide-in-from-top-2">
      <div className="flex items-start justify-between">
        <div className="flex items-center">
          <div className="flex-shrink-0">
            <AlertTriangle className="h-6 w-6 text-red-500" />
          </div>
          <div className="ml-3">
            <h3 className="text-lg font-medium text-red-100">
              Crash Dump Detected
            </h3>
            <div className="mt-1 text-sm text-red-200">
              <p>
                The device has recovered from a crash. You can view the stacktrace
                to debug the issue or clear it to dismiss this message.
              </p>
            </div>
            <div className="mt-4 flex space-x-3">
              <button
                onClick={onView}
                className="inline-flex items-center px-3 py-2 border border-transparent text-sm leading-4 font-medium rounded-md text-red-900 bg-red-100 hover:bg-red-200 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-red-500 transition-colors"
              >
                <FileText className="h-4 w-4 mr-2" />
                View Stacktrace
              </button>
              <button
                onClick={onClear}
                className="inline-flex items-center px-3 py-2 border border-red-400 text-sm leading-4 font-medium rounded-md text-red-100 bg-transparent hover:bg-red-900/50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-red-500 transition-colors"
                id="coredump-erase-btn"
              >
                <Trash2 className="h-4 w-4 mr-2" />
                Clear
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
