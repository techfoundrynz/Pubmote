import { Info } from 'lucide-react'
import { Dropdown } from './ui/Dropdown'
import { SiDiscord, SiGithub } from '@icons-pack/react-simple-icons';

export function HeaderLinksSelector() {
  const handleLinkSelect = (url: string) => {
    window.open(url, '_blank');
  };

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <Dropdown
        options={[
          {
            value: "https://github.com/techfoundrynz/Pubmote",
            tooltip: `Pubmote GitHub repository`,
            icon: <SiGithub className="h-4 w-4" />,
            label: (
              <div className="flex items-center justify-between w-full">
                <div className="flex-1 truncate">
                  <span className="mr-2">Pubmote GitHub repository</span>
                </div>
              </div>
            )
          },
          {
            value: "https://github.com/Relys/vesc_pkg/tree/float-accessories",
            tooltip: `Float Accessories GitHub repository`,
            icon: <SiGithub className="h-4 w-4" />,
            label: (
              <div className="flex items-center justify-between w-full">
                <div className="flex-1 truncate">
                  <span className="mr-2">Float Accessories GitHub repository</span>
                </div>
              </div>
            ),
          },
          {
            value: "https://discord.gg/7hTnbgPfKt",
            tooltip: `Pubmote discussion channel in PubWheel Discord`,
            icon: <SiDiscord className="h-4 w-4" />,
            label: (
              <div className="flex items-center justify-between w-full">
                <div className="flex-1 truncate">
                  <span className="mr-2">Pubmote on PubWheel Discord</span>
                </div>
              </div>
            )
          }
        ]}
        value=""
        onChange={(value) => handleLinkSelect(value as string)}
        icon={<Info className="h-4 w-4" />}
        label="Useful Links"
        width="fixed"
        dropdownWidth="auto"
        />
      </div>
    </div>
  );
}