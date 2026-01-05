import React from "react";
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
  const dropdownRef = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (
        dropdownRef.current &&
        !dropdownRef.current.contains(event.target as Node)
      ) {
        setIsOpen(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, []);

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

  const getDisplayLabel = () => {
    if (multiple) {
      return label;
    }
    const selectedOption = options.find((opt) => opt.value === value);
    return selectedOption ? selectedOption.label : label;
  };

  const getOptionTooltip = (option: DropdownOption) => {
    if (option.tooltip) return option.tooltip;
    if (typeof option.label === "string") return option.label;
    return "";
  };

  const getDropdownWidth = () => {
    if (typeof dropdownWidth === "number") return `${dropdownWidth}px`;
    if (dropdownWidth === "auto") return "auto";
    return "100%";
  };

  return (
    <div
      className={`relative ${width === "fixed" ? "w-45" : ""} ${className}`}
      ref={dropdownRef}
    >
      <button
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

      {isOpen && (
        <div
          style={{ width: getDropdownWidth() }}
          className={`absolute right-0 mt-1 bg-[var(--color-bg-secondary)] border border-gray-600 rounded-lg shadow-lg py-1 z-10 min-w-[200px]`}
        >
          {multiple && (
            <div className="px-3 py-2 text-xs font-medium text-gray-400 uppercase border-b border-gray-800">
              {multipleLabel}
            </div>
          )}
          <div className="max-h-48 overflow-y-auto px-2">
            {options.map((option) => (
              <button
                key={option.value}
                onClick={() => handleOptionClick(option.value)}
                title={getOptionTooltip(option)}
                className="flex w-full items-center gap-2 px-2 py-1.5 hover:bg-[#2a2a2a] rounded cursor-pointer"
              >
                {multiple ? (
                  <input
                    type="checkbox"
                    checked={isSelected(option.value)}
                    onChange={() => handleOptionClick(option.value)}
                    className={`rounded border-gray-600 ${
                      option.color || "text-blue-500"
                    } focus:ring-blue-500 focus:ring-offset-gray-900`}
                  />
                ) : (
                  isSelected(option.value) && (
                    <span className="w-4 h-4 flex-shrink-0">
                      <Check className="h-4 w-4 text-blue-500" />
                    </span>
                  )
                )}
                {option.icon && <span>{option.icon}</span>}
                <span className="text-sm text-[var(--color-text-primary)] truncate">
                  {option.label}
                </span>
              </button>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
