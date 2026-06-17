import { cn } from '../../utils/cn';

interface BadgeProps {
  children: React.ReactNode;
  variant?: 'default' | 'secondary' | 'destructive' | 'outline' | 'warning' | 'success' | 'info';
  size?: 'sm' | 'md' | 'lg';
  className?: string;
}

export function Badge({ children, variant = 'default', size = 'sm', className }: BadgeProps) {
  return (
    <span
      className={cn(
        'inline-flex items-center rounded-full font-medium transition-colors',
        {
          // Variants
          'bg-blue-500/10 text-blue-500': variant === 'default',
          'bg-gray-500/10 text-gray-500': variant === 'secondary',
          'bg-red-500/10 text-red-500': variant === 'destructive',
          'bg-yellow-500/10 text-yellow-500': variant === 'warning',
          'bg-green-500/10 text-green-500': variant === 'success',
          'bg-cyan-500/10 text-cyan-500': variant === 'info',
          'border border-gray-200 dark:border-gray-800': variant === 'outline',
          
          // Sizes
          'px-2 py-0.5 text-xs': size === 'sm',
          'px-2.5 py-0.5 text-sm': size === 'md',
          'px-3 py-1 text-sm': size === 'lg',
        },
        className
      )}
    >
      {children}
    </span>
  );
}