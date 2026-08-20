export type AuthMode =
  | "open"
  | "wpa-psk"
  | "wpa2-psk"
  | "wpa-wpa2-psk"
  | "wpa3-psk"
  | "wpa2-wpa3-psk";
export type Bandwidth = "ht20" | "ht40";
export type SecondaryChannel = "none" | "above" | "below";

export interface ApConfig {
  schemaVersion: number;
  ssid: string;
  ssidHidden: boolean;
  apPasswordConfigured: boolean;
  apPassword?: string;
  authMode: AuthMode;
  pairwiseCipher: string;
  pmf: string;
  saePwe: string;
  primaryChannel: number;
  bandwidth: Bandwidth;
  secondaryChannel: SecondaryChannel;
  csaCount: number;
  protocol: string;
  maxTxPowerQuarterDbm: number;
  maxClients: number;
  beaconIntervalTu: number;
  dtimPeriod: number;
}

export interface Operation {
  id: string;
  type: "configuration" | "channel-switch" | "none";
  success: boolean;
  rollbackAttempted: boolean;
  rollbackSucceeded: boolean;
  error: number;
  message: string;
}

export interface Status {
  started: boolean;
  degraded: boolean;
  managementAddress: string;
  uptimeMs: number;
  bootId: number;
  clientCount: number;
  appliedConfig: ApConfig;
  latestOperation: Operation | null;
  automationTokenCount: number;
}

export interface Capabilities {
  schemaVersion: number;
  authModes: AuthMode[];
  pairwiseCiphers: string[];
  pmfModes: string[];
  saePweMethods: string[];
  protocols: string[];
  bandwidths: Bandwidth[];
  secondaryChannels: SecondaryChannel[];
  primaryChannels: number[];
  txPowerQuarterDbm: number[];
  limits: {
    maxClients: number;
    maxTokens: number;
    eventCapacity: number;
  };
}

export interface Client {
  mac: string;
  rssi: number;
}

export interface RuntimeEvent {
  bootId: number;
  sequence: number;
  uptimeMs: number;
  severity: "debug" | "info" | "warning" | "error";
  type: string;
  details: Record<string, unknown>;
}

export interface TokenSummary {
  id: string;
  label: string;
}
