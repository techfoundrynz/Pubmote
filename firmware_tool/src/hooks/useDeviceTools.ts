import React from "react";
import DeviceToolsContext, { DeviceToolsContextType } from "../context/DeviceToolsContext";

const useDeviceTools = (): DeviceToolsContextType => {
  const context = React.useContext(DeviceToolsContext);
  if (!context) {
    throw new Error("useDeviceTools must be used within a DeviceToolsProvider");
  }
  return context;
};

export default useDeviceTools;
