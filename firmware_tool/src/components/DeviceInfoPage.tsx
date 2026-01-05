import React from "react";
import { FloatAccessoriesSelector } from "./FloatAccessoriesSelector";

const DeviceInfoPage: React.FC<unknown> = () => {
  return (
    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6">
      <FloatAccessoriesSelector />
    </div>
  );
};

export default DeviceInfoPage;
