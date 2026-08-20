import {
  expect,
  test,
  type Page,
  type Request,
  type TestInfo,
} from "@playwright/test";

const config = {
  schemaVersion: 1,
  ssid: "AirScope-Lab",
  ssidHidden: false,
  apPasswordConfigured: true,
  authMode: "wpa2-psk",
  pairwiseCipher: "ccmp",
  pmf: "optional",
  saePwe: "both",
  primaryChannel: 6,
  bandwidth: "ht20",
  secondaryChannel: "none",
  csaCount: 3,
  protocol: "bgn",
  maxTxPowerQuarterDbm: 80,
  maxClients: 6,
  beaconIntervalTu: 100,
  dtimPeriod: 2,
};

const capabilities = {
  schemaVersion: 1,
  authModes: [
    "open",
    "wpa-psk",
    "wpa2-psk",
    "wpa-wpa2-psk",
    "wpa3-psk",
    "wpa2-wpa3-psk",
  ],
  pairwiseCiphers: ["none", "tkip", "ccmp", "tkip-ccmp", "gcmp", "gcmp256"],
  pmfModes: ["disabled", "optional", "required"],
  saePweMethods: ["hunt-and-peck", "hash-to-element", "both"],
  protocols: ["b", "bg", "bgn", "gn"],
  bandwidths: ["ht20", "ht40"],
  secondaryChannels: ["none", "above", "below"],
  primaryChannels: Array.from({ length: 13 }, (_, index) => index + 1),
  txPowerQuarterDbm: [32, 44, 52, 60, 68, 80],
  limits: { maxClients: 10, maxTokens: 8, eventCapacity: 128 },
};

function status(degraded = false) {
  return {
    started: true,
    degraded,
    managementAddress: "192.168.4.1",
    uptimeMs: 7_560_000,
    bootId: 42,
    clientCount: 1,
    appliedConfig: config,
    latestOperation: {
      id: "op-42",
      type: degraded ? "configuration" : "channel-switch",
      success: !degraded,
      rollbackAttempted: degraded,
      rollbackSucceeded: false,
      error: degraded ? 259 : 0,
      message: degraded ? "Runtime rollback verification failed" : "Channel applied",
    },
    automationTokenCount: 1,
  };
}

type MockOptions = {
  degraded?: boolean;
  initiallyAuthenticated?: boolean;
  onMutation?: (request: Request, body: Record<string, unknown>) => void;
};

async function mockApi(page: Page, options: MockOptions = {}) {
  let authenticated = options.initiallyAuthenticated ?? false;
  let tokens = [{ id: "tok-existing", label: "CI runner" }];

  await page.route("**/api/v1/**", async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname;
    const method = request.method();

    if (path === "/api/v1/session" && method === "POST") {
      authenticated = true;
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({ csrfToken: "test-csrf" }),
      });
      return;
    }

    if (!authenticated) {
      await route.fulfill({
        status: 401,
        contentType: "application/json",
        body: JSON.stringify({ error: { message: "Authentication required" } }),
      });
      return;
    }

    if (path === "/api/v1/session" && method === "DELETE") {
      authenticated = false;
      await route.fulfill({ status: 204 });
      return;
    }

    if (method !== "GET") {
      const body = request.postDataJSON() as Record<string, unknown>;
      options.onMutation?.(request, body);
    }

    if (path === "/api/v1/status") {
      await route.fulfill({ json: status(options.degraded) });
    } else if (path === "/api/v1/capabilities") {
      await route.fulfill({ json: capabilities });
    } else if (path === "/api/v1/config") {
      await route.fulfill({
        json: method === "GET"
          ? config
          : { accepted: true, reconnectRequired: true, applyDelayMs: 1500 },
      });
    } else if (path === "/api/v1/channel-switch") {
      await route.fulfill({
        json: { operationId: "op-channel", result: { success: true } },
      });
    } else if (path === "/api/v1/clients") {
      await route.fulfill({
        json: { clients: [{ mac: "02:00:00:12:34:56", rssi: -58 }], count: 1 },
      });
    } else if (path === "/api/v1/events") {
      await route.fulfill({
        json: {
          events: [{
            bootId: 42,
            sequence: 7,
            uptimeMs: 7_500_000,
            severity: "info",
            type: "station.connected",
            details: { mac: "02:00:00:12:34:56" },
          }],
          count: 1,
        },
      });
    } else if (path === "/api/v1/tokens" && method === "GET") {
      await route.fulfill({ json: { tokens } });
    } else if (path === "/api/v1/tokens" && method === "POST") {
      tokens = [...tokens, { id: "tok-created", label: "bench-agent" }];
      await route.fulfill({
        status: 201,
        json: {
          id: "tok-created",
          label: "bench-agent",
          token: "as_v1_one_time_secret",
        },
      });
    } else if (path.startsWith("/api/v1/tokens/") && method === "DELETE") {
      await route.fulfill({ status: 204 });
    } else {
      await route.fulfill({ status: 204 });
    }
  });
}

async function signIn(page: Page) {
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Sign in to the access point" })).toBeVisible();
  await page.getByLabel("Management password").fill("management-secret");
  await page.getByRole("button", { name: "Sign in" }).click();
  await expect(page.getByRole("heading", { name: "AirScope-Lab" })).toBeVisible();
}

async function navigate(page: Page, name: string) {
  const mobileMenu = page.getByTitle("Open navigation");
  const isMobile = await mobileMenu.isVisible();
  if (isMobile) await mobileMenu.click();
  await page.getByRole("navigation").getByRole("button", { name }).click();
  if (isMobile) {
    await expect(page.locator(".sidebar")).not.toHaveClass(/\bopen\b/);
    await page.waitForTimeout(250);
  }
}

async function expectNoHorizontalOverflow(page: Page) {
  const dimensions = await page.evaluate(() => ({
    viewport: document.documentElement.clientWidth,
    content: document.documentElement.scrollWidth,
  }));
  expect(dimensions.content).toBeLessThanOrEqual(dimensions.viewport);
}

test("login, degraded status, and session expiry return to login", async ({ page }) => {
  await mockApi(page, { degraded: true });
  await signIn(page);

  await expect(page.getByText("Configuration degraded", { exact: true })).toBeVisible();
  await expect(page.getByText("Runtime rollback failed", { exact: false })).toBeVisible();

  await page.route("**/api/v1/status", (route) =>
    route.fulfill({
      status: 401,
      contentType: "application/json",
      body: JSON.stringify({ error: { message: "Session expired" } }),
    }),
  );
  await page.getByTitle("Refresh").click();
  await expect(page.getByRole("heading", { name: "Sign in to the access point" })).toBeVisible();
});

test("configuration validates input, applies preset, and preserves password", async ({ page }) => {
  let saved: Record<string, unknown> | undefined;
  await mockApi(page, {
    onMutation(request, body) {
      if (new URL(request.url()).pathname === "/api/v1/config") {
        expect(request.headers()["x-csrf-token"]).toBe("test-csrf");
        saved = body;
      }
    },
  });
  await signIn(page);
  await navigate(page, "Configuration");

  const ssid = page.getByRole("textbox", { name: "SSID", exact: true });
  await ssid.fill("");
  await page.getByRole("button", { name: "Apply changes" }).click();
  await expect(page.getByText("SSID is required.")).toBeVisible();

  await ssid.fill("AirScope-Test");
  await page.locator("button.preset").filter({
    has: page.getByText("WPA3", { exact: true }),
  }).click();
  await expect(page.getByLabel("Authentication")).toHaveValue("wpa3-psk");
  await expect(page.getByLabel("PMF")).toHaveValue("required");
  await page.getByRole("button", { name: "Apply changes" }).click();

  await expect.poll(() => saved).toBeTruthy();
  expect(saved).toMatchObject({
    ssid: "AirScope-Test",
    authMode: "wpa3-psk",
    pairwiseCipher: "ccmp",
    pmf: "required",
    primaryChannel: 6,
  });
  expect(saved).not.toHaveProperty("apPasswordConfigured");
  expect(saved).not.toHaveProperty("apPassword");
  await expect(page.getByText(
    'Changes accepted. Reconnect to "AirScope-Test" with the new settings.',
  )).toBeVisible();
});

test("channel switching uses its dedicated request schema", async ({ page }) => {
  let switched: Record<string, unknown> | undefined;
  await mockApi(page, {
    onMutation(request, body) {
      if (new URL(request.url()).pathname === "/api/v1/channel-switch") {
        expect(request.headers()["x-csrf-token"]).toBe("test-csrf");
        switched = body;
      }
    },
  });
  await signIn(page);
  await navigate(page, "Channel");

  await page.getByRole("button", { name: "11 2462" }).click();
  await page.getByLabel("Bandwidth").selectOption("ht40");
  await page.getByLabel("CSA beacon count").fill("5");
  await page.getByRole("button", { name: "Switch channel" }).click();

  await expect(page.getByText("Channel 11 is applied and persisted.")).toBeVisible();
  expect(switched).toEqual({
    primaryChannel: 11,
    bandwidth: "ht40",
    secondaryChannel: "below",
    csaCount: 5,
  });
});

test("new automation token is revealed once and can be dismissed", async ({ page }) => {
  let createBody: Record<string, unknown> | undefined;
  await mockApi(page, {
    onMutation(request, body) {
      if (new URL(request.url()).pathname === "/api/v1/tokens" &&
          request.method() === "POST") {
        expect(request.headers()["x-csrf-token"]).toBe("test-csrf");
        createBody = body;
      }
    },
  });
  await signIn(page);
  await navigate(page, "Access");

  await page.getByLabel("Token label").fill("bench-agent");
  await page.getByRole("button", { name: "Create token" }).click();
  await expect(page.getByText("as_v1_one_time_secret")).toBeVisible();
  expect(createBody).toEqual({ label: "bench-agent" });
  await page.getByTitle("Dismiss").click();
  await expect(page.getByText("as_v1_one_time_secret")).toBeHidden();
  await expect(page.getByText("bench-agent", { exact: true })).toBeVisible();
});

test("responsive views render without horizontal overflow", async (
  { page },
  testInfo: TestInfo,
) => {
  await mockApi(page);
  await signIn(page);
  await expectNoHorizontalOverflow(page);
  await page.screenshot({
    path: testInfo.outputPath("overview.png"),
    fullPage: true,
  });

  await navigate(page, "Configuration");
  await expect(page.getByRole("heading", { name: "AP configuration" })).toBeVisible();
  await expectNoHorizontalOverflow(page);

  await navigate(page, "Channel");
  await expect(page.getByRole("heading", { name: "Channel control" })).toBeVisible();
  await expectNoHorizontalOverflow(page);
  await page.screenshot({
    path: testInfo.outputPath("channel.png"),
    fullPage: true,
  });

  await navigate(page, "Access");
  await expect(page.getByRole("heading", { name: "Credentials and tokens" })).toBeVisible();
  await expectNoHorizontalOverflow(page);
});
