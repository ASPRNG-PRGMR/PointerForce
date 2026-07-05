(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);

  const statusDot = $("statusDot");
  const statusText = $("statusText");
  const statusVersion = $("statusVersion");
  const statusCounts = $("statusCounts");
  const reloadBtn = $("reloadBtn");
  const themeToggleBtn = $("themeToggleBtn");

  const deviceList = $("deviceList");
  const devicesModalOverlay = $("devicesModalOverlay");
  const devicesModalList = $("devicesModalList");
  const devicesModalCount = $("devicesModalCount");
  const devicesModalCloseBtn = $("devicesModalCloseBtn");
  const DEVICES_VISIBLE = 2;
  const bindingsBody = $("bindingsBody");
  const bindingsEmpty = $("bindingsEmpty");
  const bindingsScope = $("bindingsScope");
  const addBindingBtn = $("addBindingBtn");
  const addForm = $("addForm");
  const cancelAddBtn = $("cancelAddBtn");
  const fDevice = $("fDevice");
  const fKey = $("fKey");
  const fCommand = $("fCommand");
  const formError = $("formError");

  const mouseSvg = $("mouseSvg");
  const getMouseZones = () => mouseSvg.querySelectorAll(".mouse-zone");
  const editingKeyLabel = $("editingKeyLabel");
  const editingCurrentLabel = $("editingCurrentLabel");

  // Coordinates live in the mouse SVG's own viewBox (0 0 240 340), matching
  // the Basilisk-style graphic's own side/DPI button shapes.
  const EXTRA_DEFS = {
    side1: { toggle: $("extraSide1Toggle"), input: $("extraSide1Key"), shape: "path", d: "M25,145 L40,150 L40,185 L25,180 Z", defaultKey: "BTN_SIDE" },
    side2: { toggle: $("extraSide2Toggle"), input: $("extraSide2Key"), shape: "path", d: "M25,190 L40,195 L40,230 L25,225 Z", defaultKey: "BTN_EXTRA" },
    dpi:   { toggle: $("extraDpiToggle"),   input: $("extraDpiKey"),   shape: "path", d: "M108,115 L132,115 L128,135 L112,135 Z", defaultKey: "BTN_TASK" },
  };
  const EXTRAS_STORAGE_KEY = "pointerforce_mouse_extras";

  const eventTicker = $("eventTicker");
  const liveDot = $("liveDot");
  const liveLabel = $("liveLabel");

  const eventsModalOverlay = $("eventsModalOverlay");
  const eventsModalColumns = $("eventsModalColumns");
  const eventsModalCount = $("eventsModalCount");
  const eventsModalCloseBtn = $("eventsModalCloseBtn");

  const MAX_EVENTS = 30;
  const EVENT_COLUMNS = 3;
  const EVENTS_PER_COLUMN = 10;
  let eventLog = []; // newest first, capped at MAX_EVENTS

  const toastEl = $("toast");
  const daemonHero = $("daemonHero");

  const tabDashboardBtn = $("tabDashboardBtn");
  const tabConfigBtn = $("tabConfigBtn");
  const dashboardView = $("dashboardView");
  const configView = $("configView");

  const configPathLabel = $("configPathLabel");
  const toggleRawBtn = $("toggleRawBtn");
  const reloadDaemonBtn = $("reloadDaemonBtn");
  const configStructured = $("configStructured");
  const configRaw = $("configRaw");
  const deviceConfigList = $("deviceConfigList");
  const addDeviceBtn = $("addDeviceBtn");
  const addDeviceForm = $("addDeviceForm");
  const cancelAddDeviceBtn = $("cancelAddDeviceBtn");
  const dfId = $("dfId");
  const dfMatchType = $("dfMatchType");
  const dfMatchValue = $("dfMatchValue");
  const dfGrab = $("dfGrab");
  const addDeviceError = $("addDeviceError");
  const rawJsonTextarea = $("rawJsonTextarea");
  const rawJsonStatus = $("rawJsonStatus");
  const saveRawBtn = $("saveRawBtn");
  const revertRawBtn = $("revertRawBtn");
  const reloadOutput = $("reloadOutput");
  const configPathReadout = $("configPathReadout");

  let devices = [];
  let bindings = [];
  let selectedDevice = null; // null = show all
  let didAutoSelectDevice = false;

  let configPath = "";
  let configData = null;
  let rawDirty = false;

  // ---------------- helpers ----------------

  function toast(message, kind) {
    toastEl.textContent = message;
    toastEl.className = "toast" + (kind ? " " + kind : "");
    toastEl.hidden = false;
    clearTimeout(toast._t);
    toast._t = setTimeout(() => { toastEl.hidden = true; }, 3200);
  }

  async function api(path, options) {
    const res = await fetch(path, options);
    let body = null;
    try { body = await res.json(); } catch (_) { /* no body */ }
    if (!res.ok) {
      const msg = (body && body.error) || `request failed (${res.status})`;
      throw new Error(msg);
    }
    return body;
  }

  function emptyIllo(message) {
    return `
      <div class="empty-state-illo">
        <svg viewBox="0 0 100 160" class="illo-mouse illo-pulse" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" stroke-dasharray="10 5">
          <rect x="20" y="10" width="60" height="120" rx="30"></rect>
          <line x1="50" y1="10" x2="50" y2="40" stroke-dasharray="0"></line>
          <line x1="20" y1="60" x2="80" y2="60" stroke-width="1" stroke-dasharray="0"></line>
          <rect class="illo-wheel" x="46" y="25" width="8" height="20" rx="4" stroke-dasharray="0"></rect>
        </svg>
        <p>${escapeHtml(message)}</p>
      </div>
    `;
  }

  function timeNow() {
    const d = new Date();
    const p = (n, w = 2) => String(n).padStart(w, "0");
    return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${p(d.getMilliseconds(), 3)}`;
  }

  // ---------------- status ----------------

  async function refreshStatus() {
    try {
      const s = await api("/api/status");
      const running = (s.running || "").toLowerCase() === "yes";
      statusDot.className = "status-dot " + (running ? "ok" : "bad");
      statusText.textContent = running ? "daemon running" : "daemon not running";
      statusVersion.textContent = s.version ? `v${s.version}` : "\u2014";
      statusCounts.textContent =
        (s.devices ?? "?") + " devices \u00b7 " + (s.bindings ?? "?") + " bindings";
      daemonHero.classList.toggle("show", !running);
    } catch (err) {
      statusDot.className = "status-dot bad";
      statusText.textContent = "unreachable";
      statusVersion.textContent = "\u2014";
      statusCounts.textContent = err.message;
      daemonHero.classList.add("show");
    }
  }

  // ---------------- devices ----------------

  async function refreshDevices() {
    try {
      devices = await api("/api/devices");
    } catch (err) {
      deviceList.innerHTML = `<li class="empty-state">${escapeHtml(err.message)}</li>`;
      return;
    }
    renderDevices();
    populateDeviceSelect();

    if (!didAutoSelectDevice && devices.length === 1 && selectedDevice === null) {
      didAutoSelectDevice = true;
      selectedDevice = devices[0].id;
      renderDevices();
      renderBindings();
    }
  }

  function buildAllDevicesCard(onAfterSelect) {
    const allItem = document.createElement("li");
    allItem.className = "device-card";
    allItem.style.cursor = "pointer";
    allItem.style.borderColor = selectedDevice === null ? "var(--accent-dim)" : "";
    allItem.innerHTML = `<div class="device-card-top">
        <span class="device-active-dot on"></span>all devices
      </div>`;
    allItem.addEventListener("click", () => {
      selectedDevice = null;
      renderDevices();
      renderBindings();
      if (onAfterSelect) onAfterSelect();
    });
    return allItem;
  }

  function buildDeviceCard(d, onAfterSelect) {
    const active = (d.active || "").toLowerCase() === "yes";
    const li = document.createElement("li");
    li.className = "device-card";
    li.style.cursor = "pointer";
    if (selectedDevice === d.id) li.style.borderColor = "var(--accent-dim)";
    li.innerHTML = `
      <div class="device-card-top">
        <span class="device-active-dot ${active ? "on" : ""}"></span>${escapeHtml(d.id)}
      </div>
      <div class="device-name">${escapeHtml(d.name || "")}</div>
      <div class="device-path">${escapeHtml(d.path || "")}</div>
    `;
    li.addEventListener("click", () => {
      selectedDevice = d.id;
      renderDevices();
      renderBindings();
      if (onAfterSelect) onAfterSelect();
    });
    return li;
  }

  // Only shows a couple of device cards inline - most people have at most
  // a couple of mice plugged in at once. Anything past that lives behind
  // "show more", same pattern as the live events panel.
  function renderDevices() {
    if (!devices.length) {
      deviceList.innerHTML = `<li>${emptyIllo("no devices matched in config.")}</li>`;
      return;
    }
    deviceList.innerHTML = "";

    if (devices.length > 1) {
      deviceList.appendChild(buildAllDevicesCard());
    }

    for (const d of devices.slice(0, DEVICES_VISIBLE)) {
      deviceList.appendChild(buildDeviceCard(d));
    }

    if (devices.length > DEVICES_VISIBLE) {
      const hidden = devices.length - DEVICES_VISIBLE;
      const moreBtn = document.createElement("button");
      moreBtn.type = "button";
      moreBtn.className = "event-show-more";
      moreBtn.textContent = `show more (${hidden} more, ${devices.length} total)`;
      moreBtn.addEventListener("click", openDevicesModal);
      deviceList.appendChild(moreBtn);
    }
  }

  function renderDevicesModal() {
    devicesModalList.innerHTML = "";
    devicesModalCount.textContent = `\u2014 ${devices.length} total`;

    if (!devices.length) {
      devicesModalList.innerHTML = `<p class="events-modal-empty">no devices matched in config.</p>`;
      return;
    }

    if (devices.length > 1) {
      devicesModalList.appendChild(buildAllDevicesCard(closeDevicesModal));
    }
    for (const d of devices) {
      devicesModalList.appendChild(buildDeviceCard(d, closeDevicesModal));
    }
  }

  function openDevicesModal() {
    renderDevicesModal();
    devicesModalOverlay.hidden = false;
  }

  function closeDevicesModal() {
    devicesModalOverlay.hidden = true;
  }

  devicesModalCloseBtn.addEventListener("click", closeDevicesModal);
  devicesModalOverlay.addEventListener("click", (e) => {
    if (e.target === devicesModalOverlay) closeDevicesModal();
  });

  function populateDeviceSelect() {
    const ids = devices.map((d) => d.id);
    if (!ids.includes("*")) ids.push("*");
    fDevice.innerHTML = ids.map((id) => `<option value="${escapeHtml(id)}">${escapeHtml(id)}</option>`).join("");
  }

  // ---------------- bindings ----------------

  async function refreshBindings() {
    try {
      bindings = await api("/api/bindings");
    } catch (err) {
      bindingsBody.innerHTML = "";
      bindingsEmpty.hidden = false;
      bindingsEmpty.textContent = err.message;
      return;
    }
    renderBindings();
  }

  function renderBindings() {
    bindingsScope.textContent = selectedDevice ? `\u2014 ${selectedDevice}` : "\u2014 all devices";

    const rows = selectedDevice ? bindings.filter((b) => b.device === selectedDevice) : bindings;

    updateMouseZones(rows);

    bindingsBody.innerHTML = "";
    bindingsEmpty.hidden = rows.length > 0;
    if (!rows.length) {
      bindingsEmpty.textContent = "stock mouse, zero rigged buttons \u2014 hit \u201c+ bind a key\u201d and give it a job.";
      return;
    }

    for (const b of rows) {
      const tr = document.createElement("tr");
      tr.dataset.device = b.device;
      tr.dataset.key = b.key;
      tr.innerHTML = `
        <td class="device-cell">${escapeHtml(b.device)}</td>
        <td class="key-cell">${escapeHtml(b.key)}</td>
        <td class="command-cell">${escapeHtml(b.command)}</td>
        <td class="col-action"><button class="btn btn-danger" data-action="unbind">unbind</button></td>
      `;
      tr.querySelector('[data-action="unbind"]').addEventListener("click", () => unbind(b.device, b.key));
      bindingsBody.appendChild(tr);
    }
  }

  function flashBindingRow(device, key) {
    const row = bindingsBody.querySelector(
      `tr[data-device="${cssEscape(device)}"][data-key="${cssEscape(key)}"]`
    );
    if (!row) return;
    row.classList.remove("flash");
    // Force reflow so the animation restarts on repeated events.
    void row.offsetWidth;
    row.classList.add("flash");
  }

  // Mouse zones are a generic key-picker (not tied to one device), so any
  // incoming event flashes the zone by key alone, same as the illustration
  // in the reference design "pressing" on real hardware input.
  function flashMouseZone(key) {
    const zone = mouseSvg.querySelector(`.mouse-zone[data-key="${cssEscape(key)}"]`);
    if (!zone) return;
    zone.classList.remove("event-flash");
    void zone.getBBox && zone.getBBox(); // force reflow so repeat events restart the animation
    zone.classList.add("event-flash");
    clearTimeout(zone._flashTimer);
    zone._flashTimer = setTimeout(() => zone.classList.remove("event-flash"), 500);
  }

  function updateMouseZones(rows) {
    const boundKeys = new Set(rows.map((b) => b.key));
    getMouseZones().forEach((zone) => {
      zone.classList.toggle("bound", boundKeys.has(zone.dataset.key));
    });
  }

  function selectMouseKey(key) {
    getMouseZones().forEach((zone) => zone.classList.toggle("active", zone.dataset.key === key));

    if (!selectedDevice) {
      editingKeyLabel.textContent = key;
      editingCurrentLabel.textContent = "pick a device on the left first, then click this button again.";
      return;
    }

    const existing = bindings.find((b) => b.device === selectedDevice && b.key === key);
    editingKeyLabel.textContent = key;
    editingCurrentLabel.innerHTML = existing
      ? `currently runs <code>${escapeHtml(existing.command)}</code> on <code>${escapeHtml(selectedDevice)}</code>`
      : `unbound on <code>${escapeHtml(selectedDevice)}</code> &mdash; enter a command below.`;

    addForm.hidden = false;
    fDevice.value = selectedDevice;
    fKey.value = key;
    fCommand.value = existing ? existing.command : "";
    fCommand.focus();
  }

  function bindZoneClick(zone) {
    zone.addEventListener("click", () => selectMouseKey(zone.dataset.key));
  }

  getMouseZones().forEach(bindZoneClick); // the three universal core zones (left/right/wheel)

  function loadExtrasState() {
    let saved = {};
    try {
      saved = JSON.parse(localStorage.getItem(EXTRAS_STORAGE_KEY) || "{}");
    } catch (_) {
      /* ignore malformed storage */
    }
    for (const name of Object.keys(EXTRA_DEFS)) {
      const def = EXTRA_DEFS[name];
      const enabled = !!(saved[name] && saved[name].enabled);
      const key = (saved[name] && saved[name].key) || def.defaultKey;
      def.toggle.checked = enabled;
      def.input.value = key;
      def.input.hidden = !enabled;
    }
  }

  function saveExtrasState() {
    const out = {};
    for (const name of Object.keys(EXTRA_DEFS)) {
      const def = EXTRA_DEFS[name];
      out[name] = { enabled: def.toggle.checked, key: def.input.value.trim() || def.defaultKey };
    }
    localStorage.setItem(EXTRAS_STORAGE_KEY, JSON.stringify(out));
  }

  function renderMouseExtraZones() {
    mouseSvg.querySelectorAll(".mouse-zone-extra").forEach((el) => el.remove());

    for (const name of Object.keys(EXTRA_DEFS)) {
      const def = EXTRA_DEFS[name];
      if (!def.toggle.checked) continue;

      const key = def.input.value.trim() || def.defaultKey;
      let el;
      if (def.shape === "path") {
        el = document.createElementNS("http://www.w3.org/2000/svg", "path");
        el.setAttribute("d", def.d);
      } else {
        el = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        el.setAttribute("x", def.x);
        el.setAttribute("y", def.y);
        el.setAttribute("width", def.w);
        el.setAttribute("height", def.h);
        el.setAttribute("rx", def.rx);
      }
      el.classList.add("mouse-zone", "mouse-zone-extra");
      el.dataset.key = key;
      bindZoneClick(el);
      mouseSvg.appendChild(el);
    }

    renderBindings(); // reapply "bound" highlighting now that the zone set changed
  }

  for (const name of Object.keys(EXTRA_DEFS)) {
    const def = EXTRA_DEFS[name];
    def.toggle.addEventListener("change", () => {
      def.input.hidden = !def.toggle.checked;
      saveExtrasState();
      renderMouseExtraZones();
    });
    def.input.addEventListener("change", () => {
      saveExtrasState();
      renderMouseExtraZones();
    });
  }

  loadExtrasState();
  renderMouseExtraZones();

  async function unbind(device, key) {
    try {
      await api("/api/unbind", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ device, key }),
      });
      toast(`unbound ${device} ${key}`, "ok");
      await Promise.all([refreshBindings(), refreshStatus()]);
    } catch (err) {
      toast(err.message, "error");
    }
  }

  function resetMousePickerInfo() {
    getMouseZones().forEach((zone) => zone.classList.remove("active"));
    editingKeyLabel.textContent = "\u2014";
    editingCurrentLabel.textContent = "click a button on the mouse to bind it \u2014 or use \u201c+ bind a key\u201d for other devices.";
  }

  addBindingBtn.addEventListener("click", () => {
    addForm.hidden = !addForm.hidden;
    if (!addForm.hidden) {
      getMouseZones().forEach((zone) => zone.classList.remove("active"));
      editingKeyLabel.textContent = "custom";
      editingCurrentLabel.textContent = "manual entry \u2014 enter any key name and device below.";
      if (selectedDevice) fDevice.value = selectedDevice;
      fKey.focus();
    } else {
      resetMousePickerInfo();
    }
  });

  cancelAddBtn.addEventListener("click", () => {
    addForm.hidden = true;
    formError.hidden = true;
    resetMousePickerInfo();
  });

  addForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    formError.hidden = true;
    const device = fDevice.value.trim();
    const key = fKey.value.trim();
    const command = fCommand.value.trim();
    if (!device || !key || !command) return;

    try {
      await api("/api/bind", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ device, key, command }),
      });
      toast(`bound ${device} ${key}`, "ok");
      fKey.value = "";
      fCommand.value = "";
      addForm.hidden = true;
      resetMousePickerInfo();
      await Promise.all([refreshBindings(), refreshStatus()]);
    } catch (err) {
      formError.textContent = err.message;
      formError.hidden = false;
    }
  });

  async function doReload(triggerBtn, outputEl) {
    triggerBtn.disabled = true;
    try {
      const res = await api("/api/reload", { method: "POST" });
      toast("config reloaded", "ok");
      if (outputEl) {
        outputEl.hidden = false;
        outputEl.className = "reload-output ok";
        outputEl.textContent = (res && res.output) || "reloaded.";
      }
      await Promise.all([refreshStatus(), refreshDevices(), refreshBindings()]);
    } catch (err) {
      toast(err.message, "error");
      if (outputEl) {
        outputEl.hidden = false;
        outputEl.className = "reload-output error";
        outputEl.textContent = err.message;
      }
    } finally {
      triggerBtn.disabled = false;
    }
  }

  reloadBtn.addEventListener("click", () => doReload(reloadBtn, null));

  themeToggleBtn.setAttribute("aria-checked", document.documentElement.getAttribute("data-theme") === "light");

  themeToggleBtn.addEventListener("click", () => {
    const isLight = document.documentElement.getAttribute("data-theme") === "light";
    const next = isLight ? "dark" : "light";
    if (next === "dark") {
      document.documentElement.removeAttribute("data-theme"); // dark is the default, no attribute needed
    } else {
      document.documentElement.setAttribute("data-theme", "light");
    }
    localStorage.setItem("pf-theme", next);
    themeToggleBtn.setAttribute("aria-checked", next === "light");
  });

  // ---------------- config tab ----------------

  function switchTab(name) {
    const showConfig = name === "config";
    dashboardView.hidden = showConfig;
    configView.hidden = !showConfig;
    tabDashboardBtn.classList.toggle("active", !showConfig);
    tabConfigBtn.classList.toggle("active", showConfig);
    if (showConfig) loadConfig();
  }

  tabDashboardBtn.addEventListener("click", () => switchTab("dashboard"));
  tabConfigBtn.addEventListener("click", () => switchTab("config"));

  async function loadConfig() {
    try {
      const res = await api("/api/config");
      configPath = res.path;
      configData = res.config || {};
      if (!Array.isArray(configData.devices)) configData.devices = [];
      configPathLabel.textContent = configPath;
      if (configPathReadout) configPathReadout.textContent = "CONFIG: " + configPath;
      renderDeviceConfigList();
      syncRawFromConfig();
    } catch (err) {
      configPathLabel.textContent = "\u2014";
      deviceConfigList.innerHTML = `<p class="empty-state">${escapeHtml(err.message)}</p>`;
    }
  }

  // Returns [matchType, matchValue] for whichever match key is set,
  // preferring the same priority pfctl itself uses: path, then
  // vendor_product, then name.
  function matchSummary(device) {
    const match = device.match || {};
    for (const type of ["path", "vendor_product", "name"]) {
      if (match[type]) return [type, match[type]];
    }
    return [null, null];
  }

  function renderDeviceConfigList() {
    const list = configData.devices || [];
    if (!list.length) {
      deviceConfigList.innerHTML = `<p class="empty-state">no devices in config yet.</p>`;
      return;
    }
    deviceConfigList.innerHTML = "";
    for (const device of list) {
      deviceConfigList.appendChild(buildDeviceConfigCard(device));
    }
  }

  function buildDeviceConfigCard(device) {
    const card = document.createElement("div");
    card.className = "device-config-card";
    card.dataset.id = device.id;

    const [matchType, matchValue] = matchSummary(device);
    const bindingCount = device.bindings ? Object.keys(device.bindings).length : 0;
    const isWildcard = device.id === "*";

    const staticView = document.createElement("div");
    staticView.innerHTML = `
      <div class="dcc-top">
        <span class="dcc-id">${escapeHtml(device.id)}${isWildcard ? " <span class=\"panel-subtitle\">(wildcard)</span>" : ""}</span>
        <div class="dcc-actions">
          <button class="btn btn-ghost" data-action="edit">edit</button>
          <button class="btn btn-danger" data-action="delete">delete</button>
        </div>
      </div>
      <div class="dcc-meta">
        ${matchType ? `match ${escapeHtml(matchType)}: <code>${escapeHtml(matchValue)}</code>` : "no match criteria (wildcard-style)"}
        &middot; grab: ${device.grab ? "yes" : "no"}
        &middot; ${bindingCount} binding${bindingCount === 1 ? "" : "s"}
        <span class="panel-subtitle">(edit bindings from the Dashboard tab)</span>
      </div>
    `;
    card.appendChild(staticView);

    staticView.querySelector('[data-action="edit"]').addEventListener("click", () => {
      card.innerHTML = "";
      card.appendChild(buildDeviceEditForm(device, () => {
        card.innerHTML = "";
        card.appendChild(staticView);
      }));
    });

    staticView.querySelector('[data-action="delete"]').addEventListener("click", async () => {
      if (!confirm(`Delete device "${device.id}" from config? This does not unbind it live - do that from the Dashboard tab first if it's running.`)) {
        return;
      }
      try {
        await api(`/api/config/device/${encodeURIComponent(device.id)}`, { method: "DELETE" });
        toast(`removed ${device.id} from config`, "ok");
        await loadConfig();
      } catch (err) {
        toast(err.message, "error");
      }
    });

    return card;
  }

  function buildDeviceEditForm(device, onCancel) {
    const [matchType, matchValue] = matchSummary(device);
    const form = document.createElement("form");
    form.className = "dcc-edit-form";
    form.innerHTML = `
      <div class="field">
        <label>match by</label>
        <select class="ef-type">
          <option value="name">name</option>
          <option value="vendor_product">vendor_product</option>
          <option value="path">path</option>
        </select>
      </div>
      <div class="field">
        <label>match value</label>
        <input class="ef-value" type="text" required autocomplete="off" />
      </div>
      <div class="field-actions">
        <button type="submit" class="btn btn-accent">save</button>
        <button type="button" class="btn btn-ghost ef-cancel">cancel</button>
      </div>
      <div class="field field-checkbox">
        <label><input class="ef-grab" type="checkbox" /> exclusive grab</label>
      </div>
      <p class="form-error ef-error" hidden></p>
    `;
    form.querySelector(".ef-type").value = matchType || "name";
    form.querySelector(".ef-value").value = matchValue || "";
    form.querySelector(".ef-grab").checked = !!device.grab;
    form.querySelector(".ef-cancel").addEventListener("click", onCancel);

    form.addEventListener("submit", async (e) => {
      e.preventDefault();
      const errEl = form.querySelector(".ef-error");
      errEl.hidden = true;
      const type = form.querySelector(".ef-type").value;
      const value = form.querySelector(".ef-value").value.trim();
      const grab = form.querySelector(".ef-grab").checked;
      if (!value) return;

      try {
        await api(`/api/config/device/${encodeURIComponent(device.id)}`, {
          method: "PUT",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ match: { [type]: value }, grab }),
        });
        toast(`updated ${device.id}`, "ok");
        await loadConfig();
      } catch (err) {
        errEl.textContent = err.message;
        errEl.hidden = false;
      }
    });

    return form;
  }

  addDeviceBtn.addEventListener("click", () => {
    addDeviceForm.hidden = !addDeviceForm.hidden;
    if (!addDeviceForm.hidden) dfId.focus();
  });

  cancelAddDeviceBtn.addEventListener("click", () => {
    addDeviceForm.hidden = true;
    addDeviceError.hidden = true;
  });

  addDeviceForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    addDeviceError.hidden = true;
    const id = dfId.value.trim();
    const type = dfMatchType.value;
    const value = dfMatchValue.value.trim();
    const grab = dfGrab.checked;
    if (!id || !value) return;

    try {
      await api("/api/config/device", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id, match: { [type]: value }, grab }),
      });
      toast(`added ${id} to config`, "ok");
      dfId.value = "";
      dfMatchValue.value = "";
      dfGrab.checked = false;
      addDeviceForm.hidden = true;
      await loadConfig();
    } catch (err) {
      addDeviceError.textContent = err.message;
      addDeviceError.hidden = false;
    }
  });

  // ---- raw JSON editor ----

  function syncRawFromConfig() {
    rawJsonTextarea.value = JSON.stringify(configData, null, 2);
    rawJsonTextarea.classList.remove("invalid");
    rawJsonStatus.textContent = "";
    rawJsonStatus.className = "raw-json-status";
    rawDirty = false;
  }

  toggleRawBtn.addEventListener("click", () => {
    const showingRaw = !configRaw.hidden;
    if (showingRaw) {
      configRaw.hidden = true;
      configStructured.hidden = false;
      toggleRawBtn.textContent = "raw json";
    } else {
      if (!rawDirty) syncRawFromConfig();
      configRaw.hidden = false;
      configStructured.hidden = true;
      toggleRawBtn.textContent = "structured view";
    }
  });

  rawJsonTextarea.addEventListener("input", () => {
    rawDirty = true;
    try {
      JSON.parse(rawJsonTextarea.value);
      rawJsonTextarea.classList.remove("invalid");
      rawJsonStatus.textContent = "valid json";
      rawJsonStatus.className = "raw-json-status ok";
    } catch (err) {
      rawJsonTextarea.classList.add("invalid");
      rawJsonStatus.textContent = err.message;
      rawJsonStatus.className = "raw-json-status error";
    }
  });

  revertRawBtn.addEventListener("click", () => syncRawFromConfig());

  saveRawBtn.addEventListener("click", async () => {
    let parsed;
    try {
      parsed = JSON.parse(rawJsonTextarea.value);
    } catch (err) {
      rawJsonStatus.textContent = "can't save: " + err.message;
      rawJsonStatus.className = "raw-json-status error";
      return;
    }
    try {
      await api("/api/config", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ config: parsed }),
      });
      toast("config saved", "ok");
      await loadConfig();
    } catch (err) {
      rawJsonStatus.textContent = err.message;
      rawJsonStatus.className = "raw-json-status error";
    }
  });

  reloadDaemonBtn.addEventListener("click", () => doReload(reloadDaemonBtn, reloadOutput));

  // ---------------- live events ----------------

  // Parses lines like:
  //   [gaming_mouse] BTN_FORWARD  ->  playerctl next
  //   [gaming_mouse] BTN_SIDE
  function parseEventLine(line) {
    const lb = line.indexOf("[");
    const rb = line.indexOf("]", lb);
    if (lb === -1 || rb === -1) return null;
    const device = line.slice(lb + 1, rb).trim();
    let rest = line.slice(rb + 1).trim();
    if (!rest) return null;

    const spaceIdx = rest.search(/\s/);
    const key = spaceIdx === -1 ? rest : rest.slice(0, spaceIdx);
    rest = spaceIdx === -1 ? "" : rest.slice(spaceIdx).trim();

    const arrowIdx = rest.indexOf("\u2192");
    const command = arrowIdx === -1 ? "" : rest.slice(arrowIdx + 1).trim();

    return { device, key, command };
  }

  function eventLineHtml(entry) {
    if (entry.parsed) {
      return `
        <span class="event-time">${entry.time}</span>
        <span class="event-device">${escapeHtml(entry.parsed.device)}</span>
        <span class="event-key" data-full="${escapeHtml(entry.parsed.key)}">${escapeHtml(entry.parsed.key)}</span>
      `;
    }
    return `<span class="event-time">${entry.time}</span><span class="event-key" data-full="${escapeHtml(entry.raw)}">${escapeHtml(entry.raw)}</span>`;
  }

  function makeEventLineEl(entry) {
    const div = document.createElement("div");
    div.className = "event-line";
    div.innerHTML = eventLineHtml(entry);
    return div;
  }

  // Only keeps the last MAX_EVENTS events in memory (no unbounded log),
  // and only renders as many inline as fit in the panel without
  // introducing its own scrollbar. Anything past that is reachable via
  // the "show more" popup instead of scrolling.
  function renderEventTicker() {
    eventTicker.innerHTML = "";

    if (!eventLog.length) {
      eventTicker.innerHTML = `
        <div class="empty-state-illo illo-lg">
          <svg viewBox="0 0 200 300" class="illo-mouse illo-pulse" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" stroke-dasharray="10 5">
            <path d="M100 6C140 6 172 30 180 90C188 150 178 210 150 255C130 280 115 296 100 296C85 296 70 280 50 255C22 210 12 150 20 90C28 30 60 6 100 6Z"></path>
            <line x1="100" y1="8" x2="100" y2="120" stroke-dasharray="0"></line>
            <rect class="illo-wheel" x="90" y="26" width="20" height="42" rx="10" stroke-dasharray="0"></rect>
          </svg>
          <p>waiting for the first event\u2026</p>
        </div>
      `;
      return;
    }

    const frag = document.createDocumentFragment();
    const elements = eventLog.map(makeEventLineEl);
    elements.forEach((el) => frag.appendChild(el));
    eventTicker.appendChild(frag);

    // Shrink the visible list until it fits without overflowing, then
    // show a "show more" button for whatever got cut.
    let shown = elements.length;
    while (shown > 1 && eventTicker.scrollHeight > eventTicker.clientHeight) {
      eventTicker.removeChild(elements[shown - 1]);
      shown -= 1;
    }

    if (shown < eventLog.length) {
      const moreBtn = document.createElement("button");
      moreBtn.type = "button";
      moreBtn.className = "event-show-more";
      moreBtn.textContent = `show more (${eventLog.length - shown} more, ${eventLog.length} total)`;
      moreBtn.addEventListener("click", openEventsModal);

      // Make sure the button itself doesn't push the panel into overflow.
      eventTicker.appendChild(moreBtn);
      while (shown > 1 && eventTicker.scrollHeight > eventTicker.clientHeight) {
        eventTicker.removeChild(elements[shown - 1]);
        shown -= 1;
        moreBtn.textContent = `show more (${eventLog.length - shown} more, ${eventLog.length} total)`;
      }
    }
  }

  function appendEventLine(raw) {
    const parsed = parseEventLine(raw);
    if (parsed) {
      flashBindingRow(parsed.device, parsed.key);
      flashMouseZone(parsed.key);
    }

    eventLog.unshift({ time: timeNow(), raw, parsed });
    if (eventLog.length > MAX_EVENTS) eventLog.length = MAX_EVENTS;

    renderEventTicker();
  }

  // Click a pill to expand it past its truncated width; click anything
  // else on the page to collapse it back. Delegated on eventTicker since
  // rows are added/pruned constantly.
  eventTicker.addEventListener("click", (e) => {
    const pill = e.target.closest(".event-key");
    if (!pill) return;
    e.stopPropagation();
    pill.classList.toggle("is-expanded");
  });

  document.addEventListener("click", () => {
    eventTicker.querySelectorAll(".event-key.is-expanded").forEach((el) => {
      el.classList.remove("is-expanded");
    });
  });

  window.addEventListener("resize", () => {
    if (eventLog.length) renderEventTicker();
  });

  // ---- events modal ("show more") ----

  function renderEventsModal() {
    eventsModalColumns.innerHTML = "";
    eventsModalCount.textContent = `\u2014 ${eventLog.length} of last ${MAX_EVENTS}`;

    if (!eventLog.length) {
      eventsModalColumns.innerHTML = `<p class="events-modal-empty">no events yet.</p>`;
      return;
    }

    for (let c = 0; c < EVENT_COLUMNS; c++) {
      const col = document.createElement("div");
      col.className = "events-modal-column";
      const slice = eventLog.slice(c * EVENTS_PER_COLUMN, c * EVENTS_PER_COLUMN + EVENTS_PER_COLUMN);
      slice.forEach((entry) => col.appendChild(makeEventLineEl(entry)));
      eventsModalColumns.appendChild(col);
    }
  }

  function openEventsModal() {
    renderEventsModal();
    eventsModalOverlay.hidden = false;
  }

  function closeEventsModal() {
    eventsModalOverlay.hidden = true;
  }

  eventsModalColumns.addEventListener("click", (e) => {
    const pill = e.target.closest(".event-key");
    if (!pill) return;
    e.stopPropagation();
    pill.classList.toggle("is-expanded");
  });

  eventsModalCloseBtn.addEventListener("click", closeEventsModal);
  eventsModalOverlay.addEventListener("click", (e) => {
    if (e.target === eventsModalOverlay) closeEventsModal();
  });
  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    if (!eventsModalOverlay.hidden) closeEventsModal();
    if (!devicesModalOverlay.hidden) closeDevicesModal();
  });

  function connectEventStream() {
    const es = new EventSource("/api/events");

    es.onopen = () => {
      liveDot.classList.add("on");
      liveLabel.textContent = "live";
    };

    es.onmessage = (evt) => {
      if (!evt.data) return;
      appendEventLine(evt.data);
    };

    es.onerror = () => {
      liveDot.classList.remove("on");
      liveLabel.textContent = "reconnecting";
      // EventSource retries automatically; nothing else to do here.
    };
  }

  // ---------------- misc utils ----------------

  function escapeHtml(str) {
    return String(str ?? "").replace(/[&<>"']/g, (c) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    }[c]));
  }

  function cssEscape(str) {
    return String(str).replace(/["\\]/g, "\\$&");
  }

  // ---------------- boot ----------------

  async function boot() {
    await Promise.all([refreshStatus(), refreshDevices(), refreshBindings()]);
    connectEventStream();
    setInterval(refreshStatus, 8000);
  }

  boot();
})();
