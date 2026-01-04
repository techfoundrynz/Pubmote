import { Tag } from 'lucide-react';
import { FirmwareVersion, ReleaseType } from '../types';
import { Badge } from './ui/Badge';
import { Dropdown } from './ui/Dropdown';

export function FloatAccessoriesSelector() {
  const versions = [
    {
      version: "v2.5.0",
      date: "2/6/2025",
      variants: [
        {
          zipUrl: "/float_accessories/float_accessories-2.5.0.vescpkg",
          date: "2/6/2025",
          variant: "float_accessories-2.5.vescpkg"
        }
      ],
      releaseType: ReleaseType.Release
    },
    {
      version: "v2.6.0",
      date: "3/28/2025",
      variants: [
        {
          zipUrl: "/float_accessories/float_accessories-2.6.0.vescpkg",
          date: "3/28/2025",
          variant: "float_accessories-2.6.vescpkg"
        }
      ],
      releaseType: ReleaseType.Release
    },
    {
      version: "v3.1.2",
      date: "7/24/2025",
      variants: [
        {
          zipUrl: "/float_accessories/float_accessories-3.1.2.vescpkg",
          date: "7/24/2025",
          variant: "float_accessories-3.1.2.vescpkg"
        }
      ],
      releaseType: ReleaseType.Release
    },
    {
      version: "v3.2.0",
      date: "7/29/2025",
      variants: [
        {
          zipUrl: "/float_accessories/float_accessories-3.2.0.vescpkg",
          date: "5/27/2025",
          variant: "float_accessories-3.2.0.vescpkg"
        }
      ],
      releaseType: ReleaseType.Nightly
    }
  ] as FirmwareVersion[];

  const versionOptions = versions.flatMap(version =>
    version.variants.map(variant => ({
      value: variant.zipUrl,
      tooltip: `Version ${version.version} (${variant.variant}) - ${new Date(version.date).toLocaleDateString()}`,
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

  const handleVersionSelect = (url: string) => {
    window.open(url, '_blank');
  };

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold">Float Accessories Package</h2>
          <Dropdown
            options={versionOptions}
            value=""
            onChange={(value) => handleVersionSelect(value as string)}
            label="Download Float Accessories"
            icon={<Tag className="h-4 w-4" />}
            disabled={versions.length === 0}
            width="fixed"
            dropdownWidth={400}
          />
      </div>
      <div className="space-y-4">
        <div className="space-y-4 rounded-lg bg-[var(--color-bg-tertiary)] p-4">
          <p>
            A VESC Express package for controlling LEDs, BMS and Pubmote. To install:
          </p>

          <ul className="p-4" style={{listStyleType: "decimal"}}>
            <li>
              Select a build to download from the dropdown above and unzip it
            </li>
            <li>
              In VESC Tool, connect to your VESC Express
            </li>
            <li>
              Navigate to "VESC Packages" on desktop, or "Package Store" on mobile
            </li>
            <li>
              Select "Load Custom" on desktop, or "..." &gt; "Install from file..." on mobile
            </li>
            <li>
              Select the unzipped Float Accessories package (.vescpkg file)
            </li>
            <li>
              Install
            </li>
          </ul>
        </div>
      </div>
    </div>
  );
}