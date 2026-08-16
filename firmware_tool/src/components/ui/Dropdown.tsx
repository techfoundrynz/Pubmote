import React from "react";
import { createPortal } from "react-dom";
import { Check } from "lucide-react";

interface DropdownOption {
  value: string;
  label: React.ReactNode;
  icon?: React.ReactNode;
  color?: string;
  tooltip?: string;
  onClick?: () => void;
}

interface DropdownProps {
  options: DropdownOption[];
  value: string | string[];
  onChange: (value: string | string[]) => void;
  multiple?: boolean;
  label: string;
  multipleLabel?: string;
  icon?: React.ReactNode;
  disabled?: boolean;
  className?: string;
  width?: "auto" | "fixed";
  dropdownWidth?: "auto" | "button" | number;
  variant?: "default" | "icon";
}

// Viewport coordinates, anchored by the trigger's right edge so a content-sized
// menu right-aligns without being measured first
type MenuPosition = {
  right: number;
  top?: number;
  bottom?: number;
  width?: number;
  maxWidth: number;
  maxHeight: number;
};

const VIEWPORT_MARGIN = 8;
const TRIGGER_GAP = 4;
const MIN_DROP_HEIGHT = 160;

export function Dropdown({
  options,
  value,
  onChange,
  multiple = false,
  label,
  multipleLabel = "Select Options",
  icon,
  disabled = false,
  className = "",
  width = "auto",
  dropdownWidth = "button",
  variant = "default",
}: DropdownProps) {
  const [isOpen, setIsOpen] = React.useState(false);
  const [position, setPosition] = React.useState<MenuPosition | null>(null);
  const triggerRef = React.useRef<HTMLButtonElement>(null);
  const menuRef = React.useRef<HTMLDivElement>(null);

  const updatePosition = React.useCallback(() => {
    const trigger = triggerRef.current;
    if (!trigger) return;

    const rect = trigger.getBoundingClientRect();
    const spaceBelow = window.innerHeight - rect.bottom - TRIGGER_GAP - VIEWPORT_MARGIN;
    const spaceAbove = rect.top - TRIGGER_GAP - VIEWPORT_MARGIN;
    // Flip up only when there isn't usable room below
    const openUp = spaceBelow < MIN_DROP_HEIGHT && spaceAbove > spaceBelow;

    setPosition({
      right: Math.max(VIEWPORT_MARGIN, window.innerWidth - rect.right),
      top: openUp ? undefined : rect.bottom + TRIGGER_GAP,
      bottom: openUp ? window.innerHeight - rect.top + TRIGGER_GAP : undefined,
      width:
        typeof dropdownWidth === "number"
          ? dropdownWidth
          : dropdownWidth === "button"
            ? rect.width
            : undefined,
      maxWidth: window.innerWidth - VIEWPORT_MARGIN * 2,
      maxHeight: Math.max(MIN_DROP_HEIGHT, openUp ? spaceAbove : spaceBelow),
    });
  }, [dropdownWidth]);

  // Position before first paint so it can't flash in the wrong spot
  React.useLayoutEffect(() => {
    if (isOpen) updatePosition();
  }, [isOpen, updatePosition]);

  React.useEffect(() => {
    if (!isOpen) return;

    function handlePointerDown(event: MouseEvent) {
      const target = event.target as Node;
      // The portalled menu isn't inside the trigger's subtree
      if (triggerRef.current?.contains(target) || menuRef.current?.contains(target)) {
        return;
      }
      setIsOpen(false);
    }

    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape") setIsOpen(false);
    }

    // Capture scrolls from ancestor containers, not just the window
    document.addEventListener("mousedown", handlePointerDown);
    document.addEventListener("keydown", handleKeyDown);
    window.addEventListener("scroll", updatePosition, true);
    window.addEventListener("resize", updatePosition);
    return () => {
      document.removeEventListener("mousedown", handlePointerDown);
      document.removeEventListener("keydown", handleKeyDown);
      window.removeEventListener("scroll", updatePosition, true);
      window.removeEventListener("resize", updatePosition);
    };
  }, [isOpen, updatePosition]);

  const handleOptionClick = (optionValue: string) => {
    const selectedOption = options.find((o) => o.value === optionValue);
    if (selectedOption?.onClick) {
      selectedOption.onClick();
    }

    if (multiple) {
      const currentValues = Array.isArray(value) ? value : [];
      const newValues = currentValues.includes(optionValue)
        ? currentValues.length > 1
          ? currentValues.filter((v) => v !== optionValue)
          : currentValues
        : [...currentValues, optionValue];
      onChange(newValues);
    } else {
      onChange(optionValue);
      setIsOpen(false);
    }
  };

  const isSelected = (optionValue: string) => {
    if (multiple) {
      return Array.isArray(value) && value.includes(optionValue);
    }
    return value === optionValue;
  };

  const getOptionTooltip = (option: DropdownOption) => {
    if (option.tooltip) return option.tooltip;
    if (typeof option.label === "string") return option.label;
    return "";
  };

  // In document.body: inside the page flow it gets clipped by ancestor overflow
  // and painted under later stacking contexts
  const menu =
    isOpen && position
      ? createPortal(
          <div
            ref={menuRef}
            style={{
              position: "fixed",
              top: position.top,
              bottom: position.bottom,
              right: position.right,
              width: position.width,
              maxWidth: position.maxWidth,
              maxHeight: position.maxHeight,
              zIndex: 1000,
            }}
            className="flex flex-col overflow-hidden rounded-lg border border-gray-600 bg-[var(--color-bg-secondary)] py-1 shadow-lg min-w-[200px]"
          >
            {multiple && (
              <div className="flex-shrink-0 px-3 py-2 text-xs font-medium text-gray-400 uppercase border-b border-gray-800">
                {multipleLabel}
              </div>
            )}
            <div className="flex-1 overflow-y-auto px-2">
              {options.map((option) => (
                <button
                  key={option.value}
                  onClick={() => handleOptionClick(option.value)}
                  title={getOptionTooltip(option)}
                  className="flex w-full items-center gap-2 px-2 py-1.5 text-left hover:bg-[#2a2a2a] rounded cursor-pointer"
                >
                  {multiple && (
                    <input
                      type="checkbox"
                      checked={isSelected(option.value)}
                      onChange={() => handleOptionClick(option.value)}
                      className={`flex-shrink-0 rounded border-gray-600 ${
                        option.color || "text-blue-500"
                      } focus:ring-blue-500 focus:ring-offset-gray-900`}
                    />
                  )}
                  {option.icon && <span className="flex-shrink-0">{option.icon}</span>}
                  <span className="flex-1 truncate text-sm text-[var(--color-text-primary)]">
                    {option.label}
                  </span>
                  {!multiple && isSelected(option.value) && (
                    <Check className="h-4 w-4 flex-shrink-0 text-blue-500" />
                  )}
                </button>
              ))}
            </div>
          </div>,
          document.body
        )
      : null;

  return (
    <div className={`relative ${width === "fixed" ? "w-45" : ""} ${className}`}>
      <button
        ref={triggerRef}
        onClick={() => !disabled && setIsOpen(!isOpen)}
        disabled={disabled}
        title={label}
        className={`
          flex items-center justify-center transition-colors duration-200 focus:outline-none focus:ring-2 focus:ring-blue-500
          ${
            variant === "icon"
              ? `p-1 rounded hover:bg-[#2a2a2a] ${
                  disabled
                    ? "text-gray-600 cursor-not-allowed"
                    : "text-gray-400 hover:text-gray-200"
                }`
              : `gap-2 rounded-lg px-3 py-1.5 text-sm w-full border ${
                  disabled
                    ? "border-gray-700 text-gray-500 cursor-not-allowed"
                    : "border-gray-600 text-[var(--color-text-secondary)] hover:bg-[var(--color-bg-hover)] hover:border-gray-500"
                }`
          }
        `}
      >
        {variant === "icon" ? (
          icon
        ) : (
          <>
            {icon && (
              <span
                className={`flex-shrink-0 ${
                  disabled ? "opacity-50" : "text-gray-500"
                }`}
              >
                {icon}
              </span>
            )}
            <span className="flex-1 text-left truncate">{label}</span>
            <svg
              className={`h-4 w-4 flex-shrink-0 fill-current ${
                disabled ? "text-gray-600" : "text-gray-400"
              } transition-transform ${isOpen ? "rotate-180" : ""}`}
              viewBox="0 0 20 20"
            >
              <path
                fillRule="evenodd"
                d="M5.293 7.293a1 1 0 011.414 0L10 10.586l3.293-3.293a1 1 0 111.414 1.414l-4 4a1 1 0 01-1.414 0l-4-4a1 1 0 010-1.414z"
                clipRule="evenodd"
              />
            </svg>
          </>
        )}
      </button>

      {menu}
    </div>
  );
}
