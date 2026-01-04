import React from 'react';
import { X, AlertTriangle } from 'lucide-react';
import { cn } from '../../utils/cn';

interface DialogProps {
  isOpen: boolean;
  onClose: () => void;
  title: string;
  message: string;
  type?: 'error' | 'info' | 'warning';
}

export const Dialog: React.FC<DialogProps> = ({
  isOpen,
  onClose,
  title,
  message,
  type = 'error'
}) => {
  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/50 backdrop-blur-sm animate-in fade-in duration-200">
      <div className="bg-[var(--color-bg-secondary)] rounded-xl shadow-2xl border border-gray-800 max-w-md w-full overflow-hidden animate-in zoom-in-95 duration-200">
        <div className="p-6">
          <div className="flex items-start gap-4">
            <div className={cn(
              "p-3 rounded-full flex-shrink-0",
              type === 'error' ? "bg-red-900/30 text-red-500" : "bg-blue-900/30 text-blue-500"
            )}>
              <AlertTriangle className="h-6 w-6" />
            </div>
            <div className="flex-1">
              <h3 className="text-lg font-semibold text-[var(--color-text-primary)] mb-2">
                {title}
              </h3>
              <p className="text-[var(--color-text-secondary)] leading-relaxed">
                {message}
              </p>
            </div>
          </div>
        </div>
        
        <div className="bg-[var(--color-bg-tertiary)] p-4 flex justify-end">
          <button
            onClick={onClose}
            className="px-4 py-2 bg-gray-700 hover:bg-gray-600 text-white rounded-lg transition-colors font-medium text-sm"
          >
            Close
          </button>
        </div>
      </div>
    </div>
  );
};
