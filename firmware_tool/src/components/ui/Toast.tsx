import React, { useEffect } from 'react';
import { X, CheckCircle, AlertCircle, Info } from 'lucide-react';
import { cn } from '../../utils/cn';

export type ToastType = 'success' | 'error' | 'info' | 'warning';

export interface ToastProps {
  id: string;
  message: string;
  type: ToastType;
  onDismiss: (id: string) => void;
  duration?: number;
}

export const Toast: React.FC<ToastProps> = ({ 
  id, 
  message, 
  type, 
  onDismiss, 
  duration = 3000 
}) => {
  useEffect(() => {
    if (duration === 0) return;

    const timer = setTimeout(() => {
      onDismiss(id);
    }, duration);

    return () => clearTimeout(timer);
  }, [id, duration, onDismiss]);

  const icons = {
    success: <CheckCircle className="h-5 w-5 text-green-400" />,
    error: <AlertCircle className="h-5 w-5 text-red-400" />,
    warning: <AlertCircle className="h-5 w-5 text-yellow-400" />,
    info: <Info className="h-5 w-5 text-blue-400" />
  };

  const bgColors = {
    success: 'bg-[var(--color-bg-tertiary)] border-l-4 border-green-500',
    error: 'bg-[var(--color-bg-tertiary)] border-l-4 border-red-500',
    warning: 'bg-[var(--color-bg-tertiary)] border-l-4 border-yellow-500',
    info: 'bg-[var(--color-bg-tertiary)] border-l-4 border-blue-500'
  };

  return (
    <div className={cn(
      "flex items-start gap-3 p-4 rounded-lg shadow-lg min-w-[300px] max-w-md animate-in slide-in-from-right-full fade-in duration-300 pointer-events-auto",
      bgColors[type]
    )}>
      <div className="flex-shrink-0 mt-0.5">
        {icons[type]}
      </div>
      <div className="flex-1 text-sm text-gray-200">
        {message}
      </div>
      <button 
        onClick={() => onDismiss(id)}
        className="flex-shrink-0 text-gray-400 hover:text-gray-200 transition-colors"
      >
        <X className="h-4 w-4" />
      </button>
    </div>
  );
};
