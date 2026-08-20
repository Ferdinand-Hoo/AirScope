import { useEffect, useMemo, useState } from "preact/hooks";
import {
  Activity,
  Antenna,
  Check,
  ChevronRight,
  Clipboard,
  Download,
  Eye,
  EyeOff,
  Gauge,
  KeyRound,
  LogOut,
  Menu,
  Radio,
  RefreshCw,
  Save,
  Settings2,
  Shield,
  Signal,
  Trash2,
  Users,
  X,
} from "lucide-preact";
import * as api from "./api";
import type {
  ApConfig,
  Capabilities,
  Client,
  RuntimeEvent,
  Status,
  TokenSummary,
} from "./types";

type View = "overview" | "configuration" | "channel" | "clients" | "events" | "access";
type Notice = { tone: "success" | "error" | "info"; message: string };

const navigation: Array<{ id: View; label: string; icon: typeof Activity }> = [
  { id: "overview", label: "Overview", icon: Activity },
  { id: "configuration", label: "Configuration", icon: Settings2 },
  { id: "channel", label: "Channel", icon: Radio },
  { id: "clients", label: "Clients", icon: Users },
  { id: "events", label: "Events", icon: Gauge },
  { id: "access", label: "Access", icon: Shield },
];

const labels: Record<string, string> = {
  open: "Open",
  "wpa-psk": "WPA-PSK",
  "wpa2-psk": "WPA2-PSK",
  "wpa-wpa2-psk": "WPA/WPA2 mixed",
  "wpa3-psk": "WPA3-SAE",
  "wpa2-wpa3-psk": "WPA2/WPA3 transition",
  none: "None",
  tkip: "TKIP",
  ccmp: "CCMP",
  "tkip-ccmp": "TKIP + CCMP",
  gcmp: "GCMP",
  gcmp256: "GCMP-256",
  disabled: "Disabled",
  optional: "Optional",
  required: "Required",
  "hunt-and-peck": "Hunt and peck",
  "hash-to-element": "Hash to element",
  both: "Both",
  b: "802.11b",
  bg: "802.11b/g",
  bgn: "802.11b/g/n",
  gn: "802.11g/n",
  ht20: "HT20",
  ht40: "HT40",
  above: "Above",
  below: "Below",
};

const securityPresets: Array<{
  name: string;
  description: string;
  values: Pick<ApConfig, "authMode" | "pairwiseCipher" | "pmf" | "saePwe">;
}> = [
  {
    name: "WPA2",
    description: "Broad compatibility with CCMP",
    values: { authMode: "wpa2-psk", pairwiseCipher: "ccmp", pmf: "optional", saePwe: "both" },
  },
  {
    name: "WPA3",
    description: "SAE with mandatory PMF",
    values: { authMode: "wpa3-psk", pairwiseCipher: "ccmp", pmf: "required", saePwe: "both" },
  },
  {
    name: "Transition",
    description: "WPA2 and WPA3 clients",
    values: { authMode: "wpa2-wpa3-psk", pairwiseCipher: "ccmp", pmf: "required", saePwe: "both" },
  },
];

function formatDuration(milliseconds: number) {
  const seconds = Math.floor(milliseconds / 1000);
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  return days ? `${days}d ${hours}h` : hours ? `${hours}h ${minutes}m` : `${minutes}m`;
}

function title(value: string) {
  return labels[value] ?? value.replaceAll(".", " ").replaceAll("_", " ");
}

function Login({ onSuccess }: { onSuccess: () => void }) {
  const [password, setPassword] = useState("");
  const [visible, setVisible] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  async function submit(event: Event) {
    event.preventDefault();
    setBusy(true);
    setError("");
    try {
      await api.login(password);
      onSuccess();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Unable to sign in");
    } finally {
      setBusy(false);
    }
  }

  return (
    <main class="login-shell">
      <section class="login-panel">
        <div class="brand-lockup">
          <span class="brand-mark"><Antenna size={24} /></span>
          <div><strong>AirScope</strong><span>Experimental AP</span></div>
        </div>
        <div class="login-copy">
          <p class="eyebrow">LOCAL MANAGEMENT</p>
          <h1>Sign in to the access point</h1>
          <p>Use the management credential shown on the device during provisioning.</p>
        </div>
        <form onSubmit={submit}>
          <label class="field">
            <span>Management password</span>
            <span class="password-field">
              <input
                autofocus
                type={visible ? "text" : "password"}
                value={password}
                onInput={(event) => setPassword(event.currentTarget.value)}
                autocomplete="current-password"
                required
              />
              <button class="icon-button" type="button" onClick={() => setVisible(!visible)} title={visible ? "Hide password" : "Show password"}>
                {visible ? <EyeOff size={18} /> : <Eye size={18} />}
              </button>
            </span>
          </label>
          {error && <p class="form-error">{error}</p>}
          <button class="primary-button full" disabled={busy || !password}>
            {busy && <RefreshCw class="spin" size={17} />} Sign in
          </button>
        </form>
        <p class="device-address">https://192.168.4.1</p>
      </section>
    </main>
  );
}

function Metric({ label, value, detail, icon: Icon }: { label: string; value: string; detail?: string; icon: typeof Activity }) {
  return (
    <article class="metric">
      <span class="metric-icon"><Icon size={19} /></span>
      <div><span>{label}</span><strong>{value}</strong>{detail && <small>{detail}</small>}</div>
    </article>
  );
}

function Overview({ status, refresh }: { status: Status; refresh: () => void }) {
  const config = status.appliedConfig;
  return (
    <div class="view">
      <header class="view-header">
        <div><p class="eyebrow">LIVE STATUS</p><h1>{config.ssid}</h1><p>Boot {status.bootId} · up {formatDuration(status.uptimeMs)}</p></div>
        <button class="icon-button bordered" onClick={refresh} title="Refresh"><RefreshCw size={18} /></button>
      </header>
      {status.degraded && (
        <div class="critical-banner"><Shield size={20} /><div><strong>Configuration degraded</strong><span>Runtime rollback failed. Use physical recovery before further testing.</span></div></div>
      )}
      <section class="metric-grid">
        <Metric icon={Activity} label="AP state" value={status.started ? "Running" : "Stopped"} detail={status.managementAddress} />
        <Metric icon={Radio} label="Channel" value={`${config.primaryChannel} · ${labels[config.bandwidth]}`} detail={config.bandwidth === "ht40" ? `Secondary ${config.secondaryChannel}` : "20 MHz"} />
        <Metric icon={Shield} label="Security" value={labels[config.authMode]} detail={`${labels[config.pairwiseCipher]} · PMF ${config.pmf}`} />
        <Metric icon={Users} label="Clients" value={`${status.clientCount} / ${config.maxClients}`} detail={`${status.automationTokenCount} automation tokens`} />
      </section>
      <section class="section-band">
        <div class="section-heading"><div><h2>Applied radio profile</h2><p>Current driver-backed access point settings</p></div></div>
        <dl class="detail-grid">
          <div><dt>SSID broadcast</dt><dd>{config.ssidHidden ? "Hidden" : "Visible"}</dd></div>
          <div><dt>Protocol</dt><dd>{labels[config.protocol]}</dd></div>
          <div><dt>Transmit power</dt><dd>{(config.maxTxPowerQuarterDbm / 4).toFixed(1)} dBm</dd></div>
          <div><dt>Beacon interval</dt><dd>{config.beaconIntervalTu} TU</dd></div>
          <div><dt>DTIM period</dt><dd>{config.dtimPeriod}</dd></div>
          <div><dt>CSA count</dt><dd>{config.csaCount}</dd></div>
        </dl>
      </section>
      <section class="section-band">
        <div class="section-heading"><div><h2>Latest operation</h2><p>Most recent configuration or channel result</p></div></div>
        {status.latestOperation ? (
          <div class={`operation ${status.latestOperation.success ? "success" : "failure"}`}>
            {status.latestOperation.success ? <Check size={20} /> : <X size={20} />}
            <div><strong>{title(status.latestOperation.type)}</strong><span>{status.latestOperation.message}</span><small>{status.latestOperation.id}</small></div>
          </div>
        ) : <p class="empty">No runtime operation has been recorded.</p>}
      </section>
    </div>
  );
}

function SelectField({ label, value, values, onChange, disabled = false }: {
  label: string; value: string | number; values: Array<string | number>; onChange: (value: string) => void; disabled?: boolean;
}) {
  return (
    <label class="field"><span>{label}</span>
      <select value={value} disabled={disabled} onChange={(event) => onChange(event.currentTarget.value)}>
        {values.map((item) => <option value={item}>{typeof item === "string" ? title(item) : item}</option>)}
      </select>
    </label>
  );
}

function ConfigView({ initial, capabilities, onSaved }: { initial: ApConfig; capabilities: Capabilities; onSaved: () => void }) {
  const [config, setConfig] = useState<ApConfig>({ ...initial });
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<Notice | null>(null);
  const wpa3 = config.authMode === "wpa3-psk" || config.authMode === "wpa2-wpa3-psk";

  function update<K extends keyof ApConfig>(key: K, value: ApConfig[K]) {
    setConfig((current) => ({ ...current, [key]: value }));
  }

  function usePreset(preset: (typeof securityPresets)[number]) {
    setConfig((current) => ({ ...current, ...preset.values }));
  }

  function changeAuth(value: string) {
    const authMode = value as ApConfig["authMode"];
    if (authMode === "open") {
      setConfig((current) => ({ ...current, authMode, pairwiseCipher: "none", pmf: "disabled", saePwe: "both" }));
      setPassword("");
    } else {
      const isWpa3 = authMode === "wpa3-psk" || authMode === "wpa2-wpa3-psk";
      setConfig((current) => ({
        ...current,
        authMode,
        pairwiseCipher: isWpa3 ? "ccmp" : current.pairwiseCipher === "none" ? "ccmp" : current.pairwiseCipher,
        pmf: isWpa3 ? "required" : current.pmf === "disabled" ? "optional" : current.pmf,
        saePwe: "both",
      }));
    }
  }

  async function save(event: Event) {
    event.preventDefault();
    if (!config.ssid.trim()) return setNotice({ tone: "error", message: "SSID is required." });
    if (config.authMode !== "open" && !config.apPasswordConfigured && password.length < 8) {
      return setNotice({ tone: "error", message: "Set an AP password containing at least 8 characters." });
    }
    setBusy(true);
    setNotice(null);
    try {
      const { apPasswordConfigured: _configured, ...payload } = config;
      if (config.authMode === "open") payload.apPassword = "";
      else if (password) payload.apPassword = password;
      const result = await api.saveConfig(payload);
      setPassword("");
      if (result.reconnectRequired) {
        setNotice({
          tone: "success",
          message: `Changes accepted. Reconnect to "${config.ssid}" with the new settings.`,
        });
      } else {
        setNotice({ tone: "success", message: "Changes accepted and are being applied." });
        window.setTimeout(onSaved, result.applyDelayMs + 500);
      }
    } catch (reason) {
      setNotice({ tone: "error", message: reason instanceof Error ? reason.message : "Configuration failed." });
    } finally {
      setBusy(false);
    }
  }

  const allowedCiphers = config.authMode === "open"
    ? ["none"]
    : wpa3
      ? capabilities.pairwiseCiphers.filter((item) => ["ccmp", "gcmp", "gcmp256"].includes(item))
      : capabilities.pairwiseCiphers.filter((item) => ["tkip", "ccmp", "tkip-ccmp"].includes(item));

  return (
    <form class="view" onSubmit={save}>
      <header class="view-header"><div><p class="eyebrow">ATOMIC TRANSACTION</p><h1>AP configuration</h1><p>Channel fields are managed separately through live CSA.</p></div><button class="primary-button" disabled={busy}><Save size={17} /> Apply changes</button></header>
      {notice && <div class={`notice ${notice.tone}`}>{notice.message}</div>}
      <section class="section-band">
        <div class="section-heading"><div><h2>Network identity</h2><p>SSID and station capacity</p></div></div>
        <div class="form-grid">
          <label class="field span-2"><span>SSID</span><input value={config.ssid} maxlength={32} onInput={(event) => update("ssid", event.currentTarget.value)} /></label>
          <label class="toggle-field"><span><strong>Hide SSID</strong><small>Disable beacon name broadcast</small></span><input type="checkbox" checked={config.ssidHidden} onChange={(event) => update("ssidHidden", event.currentTarget.checked)} /></label>
          <label class="field"><span>Maximum clients</span><input type="number" min={1} max={capabilities.limits.maxClients} value={config.maxClients} onInput={(event) => update("maxClients", Number(event.currentTarget.value))} /></label>
        </div>
      </section>
      <section class="section-band">
        <div class="section-heading"><div><h2>Security profile</h2><p>Choose a baseline, then inspect expert settings</p></div></div>
        <div class="preset-grid">
          {securityPresets.map((preset) => (
            <button type="button" class={config.authMode === preset.values.authMode ? "preset active" : "preset"} onClick={() => usePreset(preset)}>
              <Shield size={18} /><strong>{preset.name}</strong><span>{preset.description}</span><ChevronRight size={16} />
            </button>
          ))}
        </div>
        <div class="form-grid top-gap">
          <SelectField label="Authentication" value={config.authMode} values={capabilities.authModes} onChange={changeAuth} />
          <SelectField label="Pairwise cipher" value={config.pairwiseCipher} values={allowedCiphers} onChange={(value) => update("pairwiseCipher", value)} />
          <SelectField label="PMF" value={config.pmf} values={wpa3 ? ["required"] : capabilities.pmfModes.filter((item) => config.authMode !== "open" || item === "disabled")} onChange={(value) => update("pmf", value)} />
          <SelectField label="SAE PWE" value={config.saePwe} values={capabilities.saePweMethods} onChange={(value) => update("saePwe", value)} disabled={!wpa3} />
          {config.authMode !== "open" && (
            <label class="field span-2"><span>New AP password <small>{config.apPasswordConfigured ? "Leave blank to keep current password" : "Required"}</small></span><input type="password" minlength={8} maxlength={63} value={password} onInput={(event) => setPassword(event.currentTarget.value)} autocomplete="new-password" /></label>
          )}
        </div>
      </section>
      <section class="section-band">
        <div class="section-heading"><div><h2>Radio and timing</h2><p>Non-channel parameters applied as part of this transaction</p></div></div>
        <div class="form-grid">
          <SelectField label="Protocol mode" value={config.protocol} values={capabilities.protocols} onChange={(value) => update("protocol", value)} />
          <SelectField label="Maximum transmit power" value={config.maxTxPowerQuarterDbm} values={capabilities.txPowerQuarterDbm} onChange={(value) => update("maxTxPowerQuarterDbm", Number(value))} />
          <label class="field"><span>Beacon interval (TU)</span><input type="number" min={100} max={60000} step={100} value={config.beaconIntervalTu} onInput={(event) => update("beaconIntervalTu", Number(event.currentTarget.value))} /></label>
          <label class="field"><span>DTIM period</span><input type="number" min={1} max={10} value={config.dtimPeriod} onInput={(event) => update("dtimPeriod", Number(event.currentTarget.value))} /></label>
        </div>
      </section>
    </form>
  );
}

function ChannelView({ initial, capabilities, onSwitched }: { initial: ApConfig; capabilities: Capabilities; onSwitched: () => void }) {
  const [config, setConfig] = useState({ ...initial });
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<Notice | null>(null);

  function update<K extends keyof ApConfig>(key: K, value: ApConfig[K]) {
    setConfig((current) => {
      const next = { ...current, [key]: value };
      if (key === "bandwidth" && value === "ht20") next.secondaryChannel = "none";
      if (key === "bandwidth" && value === "ht40" && next.secondaryChannel === "none") next.secondaryChannel = next.primaryChannel <= 9 ? "above" : "below";
      if (key === "primaryChannel" && next.bandwidth === "ht40") {
        if (next.primaryChannel < 5) next.secondaryChannel = "above";
        if (next.primaryChannel > 9) next.secondaryChannel = "below";
      }
      return next;
    });
  }

  const secondary = config.bandwidth === "ht20"
    ? ["none"]
    : capabilities.secondaryChannels.filter((item) =>
      (item === "above" && config.primaryChannel <= 9) ||
      (item === "below" && config.primaryChannel >= 5));

  async function submit(event: Event) {
    event.preventDefault();
    setBusy(true);
    setNotice({ tone: "info", message: `Announcing channel ${config.primaryChannel} to associated stations...` });
    try {
      await api.switchChannel(config);
      setNotice({ tone: "success", message: `Channel ${config.primaryChannel} is applied and persisted.` });
      onSwitched();
    } catch (reason) {
      setNotice({ tone: "error", message: reason instanceof Error ? reason.message : "Channel switch failed." });
    } finally {
      setBusy(false);
    }
  }

  return (
    <form class="view" onSubmit={submit}>
      <header class="view-header"><div><p class="eyebrow">LIVE CSA OPERATION</p><h1>Channel control</h1><p>The AP remains started while associated stations receive the switch announcement.</p></div><button class="primary-button" disabled={busy}><Signal size={17} /> Switch channel</button></header>
      {notice && <div class={`notice ${notice.tone}`}>{notice.message}</div>}
      <section class="channel-layout">
        <div class="channel-map" aria-label="2.4 GHz channel selector">
          {capabilities.primaryChannels.map((channel) => (
            <button type="button" class={config.primaryChannel === channel ? "channel active" : "channel"} onClick={() => update("primaryChannel", channel)}>
              <span>{channel}</span><small>{2407 + channel * 5}</small>
            </button>
          ))}
        </div>
        <div class="channel-settings">
          <SelectField label="Bandwidth" value={config.bandwidth} values={capabilities.bandwidths} onChange={(value) => update("bandwidth", value as ApConfig["bandwidth"])} />
          <SelectField label="Secondary direction" value={config.secondaryChannel} values={secondary} onChange={(value) => update("secondaryChannel", value as ApConfig["secondaryChannel"])} disabled={config.bandwidth === "ht20"} />
          <label class="field"><span>CSA beacon count</span><input type="number" min={1} max={15} value={config.csaCount} onInput={(event) => update("csaCount", Number(event.currentTarget.value))} /></label>
        </div>
      </section>
      <div class="info-strip"><Radio size={19} /><div><strong>Current request</strong><span>Channel {config.primaryChannel}, {labels[config.bandwidth]}{config.bandwidth === "ht40" ? `, secondary ${config.secondaryChannel}` : ""}, CSA count {config.csaCount}</span></div></div>
    </form>
  );
}

function ClientsView({ clients, refresh }: { clients: Client[]; refresh: () => void }) {
  return (
    <div class="view">
      <header class="view-header"><div><p class="eyebrow">ASSOCIATED STATIONS</p><h1>Clients</h1><p>{clients.length} connected to the SoftAP</p></div><button class="icon-button bordered" onClick={refresh} title="Refresh"><RefreshCw size={18} /></button></header>
      <section class="table-wrap">
        <table><thead><tr><th>Station MAC</th><th>Signal</th><th>RSSI</th></tr></thead>
          <tbody>{clients.map((client) => <tr><td class="mono">{client.mac}</td><td><span class={`signal-bars level-${client.rssi >= -55 ? 4 : client.rssi >= -65 ? 3 : client.rssi >= -75 ? 2 : 1}`}><i /><i /><i /><i /></span></td><td>{client.rssi} dBm</td></tr>)}</tbody>
        </table>
        {!clients.length && <p class="empty">No stations are currently associated.</p>}
      </section>
    </div>
  );
}

function EventsView({ events, refresh }: { events: RuntimeEvent[]; refresh: () => void }) {
  const [severity, setSeverity] = useState("all");
  const filtered = events.filter((event) => severity === "all" || event.severity === severity);
  function download() {
    const blob = new Blob([JSON.stringify({ events }, null, 2)], { type: "application/json" });
    const anchor = document.createElement("a");
    anchor.href = URL.createObjectURL(blob);
    anchor.download = `airscope-events-boot-${events[0]?.bootId ?? "unknown"}.json`;
    anchor.click();
    URL.revokeObjectURL(anchor.href);
  }
  return (
    <div class="view">
      <header class="view-header"><div><p class="eyebrow">VOLATILE RING</p><h1>Runtime events</h1><p>{events.length} events retained in memory</p></div><div class="header-actions"><button class="icon-button bordered" onClick={refresh} title="Refresh"><RefreshCw size={18} /></button><button class="icon-button bordered" onClick={download} title="Export JSON"><Download size={18} /></button></div></header>
      <div class="filter-bar">{["all", "info", "warning", "error"].map((item) => <button class={severity === item ? "active" : ""} onClick={() => setSeverity(item)}>{title(item)}</button>)}</div>
      <section class="event-list">
        {filtered.slice().reverse().map((event) => (
          <article class="event-row">
            <span class={`severity-dot ${event.severity}`} />
            <div><strong>{title(event.type)}</strong><span>{Object.keys(event.details).length ? JSON.stringify(event.details) : "No additional details"}</span></div>
            <time>+{formatDuration(event.uptimeMs)}<small>#{event.sequence}</small></time>
          </article>
        ))}
        {!filtered.length && <p class="empty">No events match this filter.</p>}
      </section>
    </div>
  );
}

function AccessView({ tokens, reload, onCredentialRotated }: { tokens: TokenSummary[]; reload: () => void; onCredentialRotated: () => void }) {
  const [label, setLabel] = useState("");
  const [newToken, setNewToken] = useState("");
  const [password, setPassword] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [notice, setNotice] = useState<Notice | null>(null);

  async function create(event: Event) {
    event.preventDefault();
    try {
      const result = await api.createToken(label);
      setNewToken(result.token);
      setLabel("");
      reload();
    } catch (reason) {
      setNotice({ tone: "error", message: reason instanceof Error ? reason.message : "Token creation failed." });
    }
  }

  async function revoke(id: string) {
    try {
      await api.revokeToken(id);
      reload();
    } catch (reason) {
      setNotice({ tone: "error", message: reason instanceof Error ? reason.message : "Token revocation failed." });
    }
  }

  async function rotate(event: Event) {
    event.preventDefault();
    if (password !== confirmation) return setNotice({ tone: "error", message: "Password confirmation does not match." });
    try {
      await api.rotateCredential(password);
      setNotice({ tone: "success", message: "Management credential rotated. Sign in again." });
      onCredentialRotated();
    } catch (reason) {
      setNotice({ tone: "error", message: reason instanceof Error ? reason.message : "Credential rotation failed." });
    }
  }

  async function copyToken() {
    await navigator.clipboard.writeText(newToken);
    setNotice({ tone: "success", message: "Token copied to clipboard." });
  }

  return (
    <div class="view">
      <header class="view-header"><div><p class="eyebrow">MANAGEMENT ACCESS</p><h1>Credentials and tokens</h1><p>Browser sessions and automation access are managed independently.</p></div></header>
      {notice && <div class={`notice ${notice.tone}`}>{notice.message}</div>}
      <section class="section-band">
        <div class="section-heading"><div><h2>Automation tokens</h2><p>Bearer credentials are shown once and stored only as hashes.</p></div><span class="count-badge">{tokens.length} active</span></div>
        <form class="inline-form" onSubmit={create}><label class="field"><span>Token label</span><input maxlength={32} value={label} onInput={(event) => setLabel(event.currentTarget.value)} placeholder="lab-controller" required /></label><button class="secondary-button"><KeyRound size={17} /> Create token</button></form>
        {newToken && (
          <div class="secret-reveal"><div><strong>New automation token</strong><span>This value cannot be retrieved again.</span></div><code>{newToken}</code><button class="icon-button" onClick={copyToken} title="Copy token"><Clipboard size={18} /></button><button class="icon-button" onClick={() => setNewToken("")} title="Dismiss"><X size={18} /></button></div>
        )}
        <div class="token-list">
          {tokens.map((token) => <div class="token-row"><span class="token-icon"><KeyRound size={17} /></span><div><strong>{token.label}</strong><code>{token.id}</code></div><button class="icon-button danger" onClick={() => revoke(token.id)} title="Revoke token"><Trash2 size={17} /></button></div>)}
          {!tokens.length && <p class="empty">No automation tokens are active.</p>}
        </div>
      </section>
      <section class="section-band">
        <div class="section-heading"><div><h2>Management credential</h2><p>Rotation immediately revokes all browser sessions and automation tokens.</p></div></div>
        <form class="form-grid credential-form" onSubmit={rotate}>
          <label class="field"><span>New password</span><input type="password" minlength={12} maxlength={63} value={password} onInput={(event) => setPassword(event.currentTarget.value)} autocomplete="new-password" required /></label>
          <label class="field"><span>Confirm password</span><input type="password" minlength={12} maxlength={63} value={confirmation} onInput={(event) => setConfirmation(event.currentTarget.value)} autocomplete="new-password" required /></label>
          <button class="secondary-button"><Shield size={17} /> Rotate credential</button>
        </form>
      </section>
    </div>
  );
}

export function App() {
  const [authenticated, setAuthenticated] = useState(false);
  const [view, setView] = useState<View>("overview");
  const [menuOpen, setMenuOpen] = useState(false);
  const [loading, setLoading] = useState(true);
  const [status, setStatus] = useState<Status | null>(null);
  const [capabilities, setCapabilities] = useState<Capabilities | null>(null);
  const [config, setConfig] = useState<ApConfig | null>(null);
  const [clients, setClients] = useState<Client[]>([]);
  const [events, setEvents] = useState<RuntimeEvent[]>([]);
  const [tokens, setTokens] = useState<TokenSummary[]>([]);

  api.setUnauthorizedHandler(() => {
    setAuthenticated(false);
    setLoading(false);
  });

  async function loadCore() {
    setLoading(true);
    try {
      const [nextStatus, nextCapabilities, nextConfig] = await Promise.all([
        api.getStatus(), api.getCapabilities(), api.getConfig(),
      ]);
      setStatus(nextStatus);
      setCapabilities(nextCapabilities);
      setConfig(nextConfig);
      setAuthenticated(true);
    } catch (reason) {
      if (!(reason instanceof api.ApiError && reason.status === 401)) console.error(reason);
    } finally {
      setLoading(false);
    }
  }

  async function loadClients() {
    const result = await api.getClients();
    setClients(result.clients);
  }
  async function loadEvents() {
    const result = await api.getEvents();
    setEvents(result.events);
  }
  async function loadTokens() {
    const result = await api.getTokens();
    setTokens(result.tokens);
  }

  useEffect(() => { loadCore(); }, []);
  useEffect(() => {
    if (!authenticated) return;
    if (view === "clients") loadClients();
    if (view === "events") loadEvents();
    if (view === "access") loadTokens();
  }, [view, authenticated]);

  const activeTitle = useMemo(() => navigation.find((item) => item.id === view)?.label, [view]);
  async function signOut() {
    try { await api.logout(); } finally { setAuthenticated(false); }
  }
  function navigate(next: View) {
    setView(next);
    setMenuOpen(false);
  }

  if (loading && !status) return <div class="boot-screen"><span class="brand-mark"><Antenna size={24} /></span><RefreshCw class="spin" size={20} /></div>;
  if (!authenticated || !status || !capabilities || !config) return <Login onSuccess={loadCore} />;

  return (
    <div class="app-shell">
      <aside class={menuOpen ? "sidebar open" : "sidebar"}>
        <div class="brand-lockup sidebar-brand"><span class="brand-mark"><Antenna size={22} /></span><div><strong>AirScope</strong><span>Experimental AP</span></div><button class="icon-button mobile-close" onClick={() => setMenuOpen(false)} title="Close navigation"><X size={19} /></button></div>
        <nav>{navigation.map(({ id, label, icon: Icon }) => <button class={view === id ? "active" : ""} onClick={() => navigate(id)}><Icon size={18} /><span>{label}</span></button>)}</nav>
        <div class="sidebar-footer">
          <div class="health"><span class={status.degraded ? "bad" : status.started ? "good" : "idle"} /><div><strong>{status.degraded ? "Degraded" : status.started ? "AP online" : "AP stopped"}</strong><span>Channel {status.appliedConfig.primaryChannel} · {status.clientCount} clients</span></div></div>
          <button class="sign-out" onClick={signOut}><LogOut size={17} /> Sign out</button>
        </div>
      </aside>
      {menuOpen && <button class="scrim" onClick={() => setMenuOpen(false)} aria-label="Close navigation" />}
      <main class="content">
        <div class="mobile-bar"><button class="icon-button" onClick={() => setMenuOpen(true)} title="Open navigation"><Menu size={21} /></button><strong>{activeTitle}</strong><span class={status.degraded ? "status-pill bad" : "status-pill"}>{status.degraded ? "Degraded" : "Online"}</span></div>
        {view === "overview" && <Overview status={status} refresh={loadCore} />}
        {view === "configuration" && <ConfigView key={`${config.ssid}-${config.authMode}`} initial={config} capabilities={capabilities} onSaved={loadCore} />}
        {view === "channel" && <ChannelView key={`${config.primaryChannel}-${config.bandwidth}`} initial={config} capabilities={capabilities} onSwitched={loadCore} />}
        {view === "clients" && <ClientsView clients={clients} refresh={loadClients} />}
        {view === "events" && <EventsView events={events} refresh={loadEvents} />}
        {view === "access" && <AccessView tokens={tokens} reload={loadTokens} onCredentialRotated={() => setTimeout(() => setAuthenticated(false), 900)} />}
      </main>
    </div>
  );
}
