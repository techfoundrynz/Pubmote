export default function init(module_or_path?: string | Request | URL): Promise<unknown>;

export function decode(bin: Uint8Array, dump: string): DecodedAddress[];

export class DecodedAddress {
  free(): void;
  address: bigint;
  function_name: string;
  location: string;
}
