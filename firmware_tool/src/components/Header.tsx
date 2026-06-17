import { Link, Link2Off, Sun, Moon } from 'lucide-react'
import { HeaderLinksSelector } from './HeaderLinksSelector'
import { useTheme } from '../context/ThemeContext'

interface HeaderProps {
  isConnected: boolean;
  isConnecting: boolean;
  onConnect: () => void;
  onDisconnect: () => void;
}

const Header: React.FC<HeaderProps> = ({ 
  isConnected, 
  isConnecting, 
  onConnect, 
  onDisconnect 
}) => {
  const { theme, toggleTheme } = useTheme();

  return (
    <header className="bg-[var(--color-bg-primary)]">
      <div className="flex justify-between items-center px-4 py-6 max-w-screen-2xl mx-auto w-full">
        <div className="flex items-center gap-3">
          <img src="/favicon.svg" alt="Pubmote Logo" className="h-12 w-12" />
          <div>
            <h1 className="text-xl font-bold text-[var(--color-text-primary)] leading-none">Pubmote Firmware Tool</h1>
            <p className="text-xs text-[var(--color-text-tertiary)] mt-1">Diagnostic tool and firmware updater</p>
          </div>
        </div>
        <div className="flex items-center gap-4">
          {isConnected ? (
            <button
              onClick={onDisconnect}
              className="flex items-center gap-2 rounded-lg px-3 py-1.5 text-sm font-medium text-white transition-colors bg-[var(--color-danger)] hover:bg-red-700 border border-red-500"
            >
              <Link2Off className="h-4 w-4" />
              Disconnect
            </button>
          ) : (
            <button
              onClick={onConnect}
              disabled={isConnecting}
              className="flex items-center gap-2 rounded-lg px-3 py-1.5 text-sm font-medium text-white transition-colors bg-blue-600 hover:bg-blue-700 disabled:bg-[var(--color-bg-disabled)] disabled:text-[var(--color-text-disabled)] disabled:cursor-not-allowed"
            >
              <Link className="h-4 w-4" />
              {isConnecting ? "Connecting..." : "Connect Device"}
            </button>
          )}
          <div className="h-6 w-px bg-gray-800"></div>
          <button
            onClick={toggleTheme}
            className="p-1.5 rounded-lg hover:bg-[var(--color-bg-hover)] transition-colors text-[var(--color-text-secondary)] hover:text-[var(--color-text-primary)]"
            title={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
          >
            {theme === 'dark' ? <Sun className="h-5 w-5" /> : <Moon className="h-5 w-5" />}
          </button>
          <div className="h-6 w-px bg-gray-800"></div>
          <HeaderLinksSelector />
        </div>
      </div>
    </header>
  );
}


export default Header;