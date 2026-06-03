(() => {
  "use strict";

  const WIDTH = 320;
  const HEIGHT = 200;
  const TILE_SIZE = 8;
  const WORLD_COLS = 80;
  const WORLD_ROWS = 50;
  const OVERLAY_COLS = 40;
  const OVERLAY_ROWS = 25;
  const BANK_COUNT = 8;
  const TILES_PER_BANK = 256;
  const TILE_PIXELS = 64;
  const CHR_WIRE_BYTES = 24;
  const OAM_RECORD_BYTES = 5;
  const OAM_COUNT = 256;
  const ACTIVE_SPRITES = 18;
  const UDP_PAYLOAD = 512;
  const PACKET_HEADER = 32;
  const SNAPSHOT_PAYLOAD = 68944;
  const SNAPSHOT_CHUNKS = Math.ceil(SNAPSHOT_PAYLOAD / UDP_PAYLOAD);
  const MAX_PACKET_HISTORY = 64;

  const paletteRgb = [
    ["#18212f", "#274760", "#3f7f80", "#74c69d", "#d9ed92", "#f8f1d9", "#f0a04b", "#c84646"],
    ["#12100f", "#3d3028", "#75513f", "#a87554", "#d9a05f", "#ffe0a3", "#7ab87a", "#45705a"],
    ["#081820", "#183860", "#3060a8", "#58a8d8", "#f8f8f8", "#f8d878", "#f87878", "#903850"],
    ["#101010", "#273036", "#49515a", "#7d8790", "#c0c9d1", "#ffffff", "#f0b849", "#42c2a4"],
    ["#120c1c", "#2d1d46", "#604b9b", "#9d7bff", "#ffd166", "#ef476f", "#06d6a0", "#f7fff7"],
    ["#092327", "#0b5351", "#00a896", "#99e2b4", "#fcfffc", "#f9c74f", "#f3722c", "#577590"],
    ["#1d1025", "#3f1f47", "#7b2d5e", "#c4496c", "#f58f7c", "#ffd6a5", "#6ad4dd", "#33658a"],
    ["#0f1a12", "#25432c", "#4f772d", "#90a955", "#ecf39e", "#f7f3e3", "#d4a373", "#6c584c"],
    ["#130f0c", "#44312b", "#85503f", "#c46d46", "#f4a259", "#f6e7cb", "#8cb369", "#2f4858"],
    ["#101419", "#222e50", "#4a5d8f", "#7d9dff", "#f4f1de", "#e07a5f", "#81b29a", "#f2cc8f"],
    ["#141414", "#3a3a3a", "#6c6c6c", "#9a9a9a", "#cfcfcf", "#ffffff", "#42c2a4", "#d85f71"],
    ["#081c15", "#1b4332", "#2d6a4f", "#52b788", "#b7e4c7", "#fff3b0", "#ee964b", "#bc4749"],
    ["#1b1725", "#534b62", "#a499b3", "#d0bcd5", "#f2e7dc", "#f0b849", "#42c2a4", "#d85f71"],
    ["#0a1128", "#001f54", "#034078", "#1282a2", "#fefcfb", "#fcbf49", "#f77f00", "#d62828"],
    ["#181818", "#233d4d", "#619b8a", "#a1c181", "#fcca46", "#fe7f2d", "#f8f5f0", "#b23a48"],
    ["#161a1d", "#343a40", "#6c757d", "#adb5bd", "#f8f9fa", "#e9c46a", "#2a9d8f", "#e76f51"],
  ].map((row) => row.map(hexToRgb));

  const dom = {
    clientCanvas: document.getElementById("client-canvas"),
    referenceCanvas: document.getElementById("reference-canvas"),
    playToggle: document.getElementById("play-toggle"),
    snapshotButton: document.getElementById("snapshot-button"),
    fpsSlider: document.getElementById("fps-slider"),
    fpsOutput: document.getElementById("fps-output"),
    rttSlider: document.getElementById("rtt-slider"),
    rttOutput: document.getElementById("rtt-output"),
    jitterSlider: document.getElementById("jitter-slider"),
    jitterOutput: document.getElementById("jitter-output"),
    bandwidthSlider: document.getElementById("bandwidth-slider"),
    bandwidthOutput: document.getElementById("bandwidth-output"),
    inflightSlider: document.getElementById("inflight-slider"),
    inflightOutput: document.getElementById("inflight-output"),
    lossSlider: document.getElementById("loss-slider"),
    lossOutput: document.getElementById("loss-output"),
    dynamicChrToggle: document.getElementById("dynamic-chr-toggle"),
    metricFrame: document.getElementById("metric-frame"),
    metricPayload: document.getElementById("metric-payload"),
    metricPackets: document.getElementById("metric-packets"),
    metricSustained: document.getElementById("metric-sustained"),
    metricSnapshot: document.getElementById("metric-snapshot"),
    metricRepairs: document.getElementById("metric-repairs"),
    metricDiff: document.getElementById("metric-diff"),
    metricFrameLag: document.getElementById("metric-frame-lag"),
    metricMode: document.getElementById("metric-mode"),
    metricClock: document.getElementById("metric-clock"),
    metricLatency: document.getElementById("metric-latency"),
    metricInflight: document.getElementById("metric-inflight"),
    metricQueue: document.getElementById("metric-queue"),
    metricRecent: document.getElementById("metric-recent"),
    packetStrip: document.getElementById("packet-strip"),
  };

  const clientCtx = dom.clientCanvas.getContext("2d");
  const referenceCtx = dom.referenceCanvas.getContext("2d");
  const referenceImage = referenceCtx.createImageData(WIDTH, HEIGHT);
  const clientImage = clientCtx.createImageData(WIDTH, HEIGHT);
  const referenceBuffer = new Uint8ClampedArray(referenceImage.data.length);
  const clientBuffer = new Uint8ClampedArray(clientImage.data.length);

  const app = {
    running: true,
    targetFps: Number(dom.fpsSlider.value),
    rttMs: Number(dom.rttSlider.value),
    jitterMs: Number(dom.jitterSlider.value),
    bandwidthKiB: Number(dom.bandwidthSlider.value),
    maxInFlight: Number(dom.inflightSlider.value),
    packetLoss: Number(dom.lossSlider.value) / 100,
    dynamicChr: dom.dynamicChrToggle.checked,
    altBanks: false,
    clockMode: "local",
    now: 0,
    nextLocalFrameAt: 0,
    nextRequestAt: 0,
    frame: 0,
    lastRequestedFrame: 0,
    sequence: 1,
    dropped: 0,
    repairs: 0,
    repairQueue: [],
    pendingPersistent: [],
    recentPacketItems: [],
    network: {
      events: [],
      downlinkNextFreeAt: 0,
      inFlight: 0,
      lastLatencyMs: 0,
    },
    lastStats: {
      payload: 0,
      headers: 0,
      packets: 0,
      recent: "snapshot",
      diff: 0,
    },
  };

  const server = createVideoState();
  const client = createVideoState();
  const spriteMeta = createSpriteMeta();

  initializeServerState(server);
  copyState(server, client);
  drawState(server, referenceImage.data);
  drawState(client, clientImage.data);
  referenceCtx.putImageData(referenceImage, 0, 0);
  clientCtx.putImageData(clientImage, 0, 0);

  dom.metricSnapshot.textContent = `${formatKiB(SNAPSHOT_PAYLOAD)} / ${SNAPSHOT_CHUNKS} pkts`;

  dom.playToggle.addEventListener("click", () => {
    app.running = !app.running;
    dom.playToggle.textContent = app.running ? "II" : ">";
    dom.playToggle.title = app.running ? "Pause simulation" : "Resume simulation";
    dom.playToggle.setAttribute("aria-label", dom.playToggle.title);
  });

  dom.snapshotButton.addEventListener("click", () => {
    app.pendingPersistent.length = 0;
    app.lastRequestedFrame = app.frame;
    scheduleResponse([makeSnapshotUpdate()], app.now);
  });

  dom.fpsSlider.addEventListener("input", () => {
    app.targetFps = Number(dom.fpsSlider.value);
    dom.fpsOutput.textContent = String(app.targetFps);
  });

  dom.rttSlider.addEventListener("input", () => {
    app.rttMs = Number(dom.rttSlider.value);
    dom.rttOutput.textContent = `${app.rttMs} ms`;
  });

  dom.jitterSlider.addEventListener("input", () => {
    app.jitterMs = Number(dom.jitterSlider.value);
    dom.jitterOutput.textContent = `${app.jitterMs} ms`;
  });

  dom.bandwidthSlider.addEventListener("input", () => {
    app.bandwidthKiB = Number(dom.bandwidthSlider.value);
    dom.bandwidthOutput.textContent = `${app.bandwidthKiB} KiB/s`;
  });

  dom.inflightSlider.addEventListener("input", () => {
    app.maxInFlight = Number(dom.inflightSlider.value);
    dom.inflightOutput.textContent = String(app.maxInFlight);
  });

  dom.lossSlider.addEventListener("input", () => {
    app.packetLoss = Number(dom.lossSlider.value) / 100;
    dom.lossOutput.textContent = `${dom.lossSlider.value}%`;
  });

  dom.dynamicChrToggle.addEventListener("change", () => {
    app.dynamicChr = dom.dynamicChrToggle.checked;
  });

  document.querySelectorAll('input[name="bank-mode"]').forEach((input) => {
    input.addEventListener("change", () => {
      app.altBanks = input.value === "alt" && input.checked;
      server.control.bgAltBank = app.altBanks ? 3 : 0;
      server.control.overlayAltBank = app.altBanks ? 4 : 1;
      client.control.bgAltBank = server.control.bgAltBank;
      client.control.overlayAltBank = server.control.overlayAltBank;
      markAltAttributes(server, app.altBanks);
      copyState(server, client);
      app.pendingPersistent.length = 0;
      app.lastRequestedFrame = app.frame;
      renderAndMeasure();
      updateMetrics();
    });
  });

  document.querySelectorAll('input[name="clock-mode"]').forEach((input) => {
    input.addEventListener("change", () => {
      if (input.checked) {
        app.clockMode = input.value;
        app.nextLocalFrameAt = app.now;
        app.nextRequestAt = app.now;
        app.lastRequestedFrame = Math.max(0, app.frame - 1);
      }
    });
  });

  updateMetrics();
  requestAnimationFrame(loop);

  function loop(now) {
    app.now = now;
    processNetwork(now);

    if (app.running && app.clockMode === "local") {
      while (now >= app.nextLocalFrameAt) {
        advanceServerFrame();
        app.nextLocalFrameAt += 1000 / app.targetFps;
      }
    }

    if (app.running && now >= app.nextRequestAt && app.network.inFlight < app.maxInFlight) {
      requestFrame(now);
      app.nextRequestAt = now + 1000 / app.targetFps;
    }

    renderAndMeasure();
    updateMetrics();
    requestAnimationFrame(loop);
  }

  function advanceServerFrame() {
    app.frame += 1;
    const updates = stepServer(app.frame);
    if (app.clockMode === "local") {
      for (const update of updates) {
        if (update.persistent) {
          app.pendingPersistent.push(update);
        }
      }
      return [];
    }
    return makeFrameStateUpdates().concat(updates);
  }

  function requestFrame(now) {
    const updates = app.clockMode === "paced" ? advanceServerFrame() : collectLatestFrameUpdates();
    const repairUpdates = collectDueRepairs(now);
    const allUpdates = repairUpdates.concat(updates);
    scheduleResponse(allUpdates, now);
  }

  function collectLatestFrameUpdates() {
    if (app.lastRequestedFrame === app.frame) {
      return app.pendingPersistent.splice(0);
    }
    app.lastRequestedFrame = app.frame;
    return app.pendingPersistent.splice(0).concat(makeFrameStateUpdates());
  }

  function stepServer(frame) {
    const updates = [];

    server.control.frameId = frame;
    server.control.scrollX = (server.control.scrollX + 1) % (WORLD_COLS * TILE_SIZE);
    server.control.scrollY = Math.floor(12 + Math.sin(frame / 80) * 10);

    updateSprites(frame);

    if (frame % 8 === 0) {
      updateStreamingColumn(frame, updates);
    }

    if (frame % 25 === 0) {
      updatePaletteAnimation(frame, updates);
    }

    if (frame % 50 === 0) {
      updateOverlayCounter(frame, updates);
    }

    if (app.dynamicChr && frame % 100 === 0) {
      updateCharacterTiles(frame, updates);
    }

    return updates;
  }

  function makeFrameStateUpdates() {
    const updates = [makeControlUpdate()];
    updates.push(makeOamUpdate(0, server.oam.slice(0, ACTIVE_SPRITES * OAM_RECORD_BYTES)));
    return updates;
  }

  function collectDueRepairs(now) {
    const due = [];
    const waiting = [];
    for (const item of app.repairQueue) {
      if (item.due <= now) {
        const repair = cloneUpdate(item.update);
        repair.repair = true;
        due.push(repair);
      } else {
        waiting.push(item);
      }
    }
    app.repairQueue = waiting;
    return due;
  }

  function scheduleResponse(updates, now) {
    let payload = 0;
    let headers = 0;
    let packets = 0;
    const recent = [];
    const response = {
      requestTime: now,
      pendingPackets: 0,
    };
    const requestArrivesAt = now + sampleOneWayLatency();
    let sendCursor = Math.max(requestArrivesAt, app.network.downlinkNextFreeAt);

    for (const update of updates) {
      const chunks = Math.max(1, Math.ceil(update.wireBytes / UDP_PAYLOAD));
      const group = {
        update,
        remaining: chunks,
        lost: false,
      };
      let remainingPayload = update.wireBytes;

      payload += update.wireBytes;
      headers += chunks * PACKET_HEADER;
      packets += chunks;
      recent.push(update.kind);

      for (let chunk = 0; chunk < chunks; chunk += 1) {
        const chunkPayload = Math.min(UDP_PAYLOAD, remainingPayload);
        const chunkWireBytes = chunkPayload + PACKET_HEADER;
        const dropped = Math.random() < app.packetLoss;
        const transmitMs = chunkWireBytes / bytesPerMs();

        remainingPayload -= chunkPayload;
        sendCursor += transmitMs;
        app.network.downlinkNextFreeAt = sendCursor;
        response.pendingPackets += 1;

        app.network.events.push({
          time: sendCursor + sampleOneWayLatency(),
          group,
          response,
          kind: update.kind,
          repair: update.repair,
          dropped,
        });
      }
    }

    if (packets > 0) {
      app.network.inFlight += 1;
      app.network.events.sort((a, b) => a.time - b.time);
    }

    app.lastStats = {
      payload,
      headers,
      packets,
      recent: summarizeRecent(recent),
      diff: app.lastStats.diff,
    };
  }

  function processNetwork(now) {
    while (app.network.events.length > 0 && app.network.events[0].time <= now) {
      const event = app.network.events.shift();
      const group = event.group;

      appendPacketVisual(event);

      if (event.dropped) {
        group.lost = true;
        app.dropped += 1;
      }

      group.remaining -= 1;
      if (group.remaining === 0) {
        if (group.lost) {
          scheduleRepair(group.update, now);
        } else {
          group.update.apply(client);
          if (group.update.repair) {
            app.repairs += 1;
          }
        }
      }

      event.response.pendingPackets -= 1;
      if (event.response.pendingPackets === 0) {
        app.network.inFlight = Math.max(0, app.network.inFlight - 1);
        app.network.lastLatencyMs = Math.max(0, now - event.response.requestTime);
      }
    }
  }

  function scheduleRepair(update, now) {
    if (!update.persistent) {
      return;
    }
    app.repairQueue.push({
      due: now + sampleRoundTripLatency(),
      update: cloneUpdate(update),
    });
  }

  function sampleOneWayLatency() {
    const jitter = (Math.random() * 2 - 1) * app.jitterMs;
    return Math.max(0, app.rttMs / 2 + jitter);
  }

  function sampleRoundTripLatency() {
    const jitter = (Math.random() * 2 - 1) * app.jitterMs;
    return Math.max(0, app.rttMs + jitter);
  }

  function bytesPerMs() {
    return Math.max(1, (app.bandwidthKiB * 1024) / 1000);
  }

  function appendPacketVisual(event) {
    app.recentPacketItems.push({
      kind: event.kind,
      repair: event.repair,
      dropped: event.dropped,
    });
    if (app.recentPacketItems.length > MAX_PACKET_HISTORY) {
      app.recentPacketItems.splice(0, app.recentPacketItems.length - MAX_PACKET_HISTORY);
    }
  }

  function renderAndMeasure() {
    drawState(server, referenceImage.data);
    drawState(client, clientImage.data);
    referenceCtx.putImageData(referenceImage, 0, 0);
    clientCtx.putImageData(clientImage, 0, 0);
    referenceBuffer.set(referenceImage.data);
    clientBuffer.set(clientImage.data);
    app.lastStats.diff = countPixelDiff(referenceBuffer, clientBuffer);
  }

  function updateMetrics() {
    const totalPerFrame = app.lastStats.payload + app.lastStats.headers;
    const sustained = totalPerFrame * app.targetFps;
    dom.metricFrame.textContent = String(app.frame);
    dom.metricPayload.textContent = `${formatBytes(app.lastStats.payload)} + ${formatBytes(app.lastStats.headers)}`;
    dom.metricPackets.textContent = String(app.lastStats.packets);
    dom.metricSustained.textContent = `${formatKiB(sustained)}/s`;
    dom.metricRepairs.textContent = `${app.dropped} / ${app.repairs}`;
    dom.metricDiff.textContent = String(app.lastStats.diff);
    dom.metricFrameLag.textContent = String(Math.max(0, server.control.frameId - client.control.frameId));
    dom.metricMode.textContent = app.altBanks ? "5 banks" : "3 banks";
    dom.metricClock.textContent = app.clockMode === "local" ? "Local" : "Video-paced";
    dom.metricLatency.textContent = `${Math.round(app.network.lastLatencyMs)} ms`;
    dom.metricInflight.textContent = `${app.network.inFlight} / ${app.maxInFlight}`;
    dom.metricQueue.textContent = `${Math.round(Math.max(0, app.network.downlinkNextFreeAt - app.now))} ms`;
    dom.metricRecent.textContent = app.lastStats.recent;
    dom.packetStrip.replaceChildren(...buildPacketVisuals(app.recentPacketItems));
  }

  function createVideoState() {
    return {
      control: {
        frameId: 0,
        scrollX: 0,
        scrollY: 0,
        bgBank: 0,
        bgAltBank: 0,
        spriteBank: 2,
        overlayBank: 1,
        overlayAltBank: 1,
      },
      palettes: new Uint8Array(16 * 8 * 3),
      chars: new Uint8Array(BANK_COUNT * TILES_PER_BANK * TILE_PIXELS),
      worldTiles: new Uint8Array(WORLD_COLS * WORLD_ROWS),
      worldAttrs: new Uint8Array(WORLD_COLS * WORLD_ROWS),
      overlayTiles: new Uint8Array(OVERLAY_COLS * OVERLAY_ROWS),
      overlayAttrs: new Uint8Array(OVERLAY_COLS * OVERLAY_ROWS),
      oam: new Uint8Array(OAM_COUNT * OAM_RECORD_BYTES),
    };
  }

  function initializeServerState(state) {
    loadPalettes(state);
    generateCharacterBanks(state);
    generateWorld(state);
    generateOverlay(state);
    initializeSprites(state);
  }

  function loadPalettes(state) {
    for (let pal = 0; pal < 16; pal += 1) {
      for (let color = 0; color < 8; color += 1) {
        const [r, g, b] = paletteRgb[pal][color];
        const offset = (pal * 8 + color) * 3;
        state.palettes[offset] = r;
        state.palettes[offset + 1] = g;
        state.palettes[offset + 2] = b;
      }
    }
  }

  function generateCharacterBanks(state) {
    for (let bank = 0; bank < BANK_COUNT; bank += 1) {
      for (let tile = 0; tile < TILES_PER_BANK; tile += 1) {
        writeTile(state, bank, tile, (x, y) => patternPixel(bank, tile, x, y));
      }
    }
    writeOverlayGlyphs(state, 1);
    writeOverlayGlyphs(state, 4);
    writeSpriteTiles(state, 2);
  }

  function generateWorld(state) {
    for (let row = 0; row < WORLD_ROWS; row += 1) {
      for (let col = 0; col < WORLD_COLS; col += 1) {
        const idx = row * WORLD_COLS + col;
        const tile = scenicWorldCell(col, row);
        state.worldTiles[idx] = tile.id;
        state.worldAttrs[idx] = tile.palette;
      }
    }
    markAltAttributes(state, false);
  }

  function scenicWorldCell(col, row) {
    const cloud = row < 5 && ((col > 7 && col < 18 && row > 1) || (col > 49 && col < 64 && row > 0));
    const peak = 7 + Math.abs(((col + 12) % 24) - 12) / 2;
    const farPeak = 9 + Math.abs(((col + 2) % 18) - 9) / 2;
    const house = col >= 54 && col <= 65 && row >= 14 && row <= 21;
    const pondEdge = 30 + Math.floor(Math.sin(col / 5) * 2);

    if (house) {
      if (row === 14 || row === 15) {
        return { id: 22, palette: 8 };
      }
      if ((col === 58 || col === 61) && row >= 17 && row <= 18) {
        return { id: 24, palette: 8 };
      }
      return { id: 23, palette: 8 };
    }
    if (cloud) {
      return { id: row < 3 ? 2 : 3, palette: 2 };
    }
    if (row < peak && row > 5) {
      return { id: row < peak - 2 ? 5 : 4, palette: 9 };
    }
    if (row < farPeak && row > 7) {
      return { id: 4, palette: 3 };
    }
    if (row < 13) {
      return { id: 1, palette: 2 };
    }
    if (row < 16) {
      return { id: (col + row) % 5 === 0 ? 6 : 1, palette: 7 };
    }
    if (row < 18) {
      return { id: (col + row) % 7 === 0 ? 7 : 8, palette: 7 };
    }
    if (row < 22) {
      return { id: (col * 3 + row) % 19 === 0 ? 12 : 8 + ((col + row) % 2), palette: 7 };
    }
    if (row < 27) {
      return { id: (col + row) % 6 === 0 ? 14 : 13, palette: 1 };
    }
    if (row >= pondEdge) {
      return { id: (col + row) % 5 === 0 ? 17 : 16, palette: 5 };
    }
    return { id: (col + row) % 11 === 0 ? 11 : 10, palette: 1 };
  }

  function markAltAttributes(state, enabled) {
    for (let row = 0; row < WORLD_ROWS; row += 1) {
      for (let col = 0; col < WORLD_COLS; col += 1) {
        const idx = row * WORLD_COLS + col;
        state.worldAttrs[idx] &= 0x7f;
        if (enabled && ((col * 17 + row * 11) % 19 === 0 || state.worldTiles[idx] > 20)) {
          state.worldAttrs[idx] |= 0x80;
        }
      }
    }

    for (let i = 0; i < state.overlayAttrs.length; i += 1) {
      state.overlayAttrs[i] &= 0x7f;
      if (enabled && i % 13 === 0) {
        state.overlayAttrs[i] |= 0x80;
      }
    }
  }

  function generateOverlay(state) {
    state.overlayTiles.fill(0);
    state.overlayAttrs.fill(0x03);

    for (let col = 0; col < OVERLAY_COLS; col += 1) {
      setOverlayCell(state, col, 0, 96, 3);
      setOverlayCell(state, col, OVERLAY_ROWS - 1, 96, 3);
    }
    for (let row = 0; row < OVERLAY_ROWS; row += 1) {
      setOverlayCell(state, 0, row, 97, 3);
      setOverlayCell(state, OVERLAY_COLS - 1, row, 97, 3);
    }

    writeOverlayText(state, 2, 1, "MIA UDP POC", 3);
    writeOverlayText(state, 25, 1, "F0000", 3);
    writeOverlayText(state, 2, 23, "ABS UPDATES", 3);
  }

  function initializeSprites(state) {
    state.oam.fill(0);
    for (let i = 0; i < OAM_COUNT; i += 1) {
      state.oam[i * OAM_RECORD_BYTES + 4] = 0x08;
    }
    updateSprites(0);
  }

  function createSpriteMeta() {
    const items = [];
    for (let i = 0; i < ACTIVE_SPRITES; i += 1) {
      items.push({
        xBase: 24 + ((i * 31) % 240),
        yBase: 44 + ((i * 23) % 118),
        speed: 0.4 + (i % 5) * 0.11,
        phase: i * 0.7,
        tile: 32 + (i % 6),
        palette: 4 + (i % 6),
      });
    }
    return items;
  }

  function updateSprites(frame) {
    for (let i = 0; i < ACTIVE_SPRITES; i += 1) {
      const meta = spriteMeta[i];
      const x = Math.round(meta.xBase + Math.sin(frame / 18 + meta.phase) * 22);
      const y = Math.round(meta.yBase + Math.sin(frame / 13 + meta.phase) * 28);
      const base = i * OAM_RECORD_BYTES;
      server.oam[base] = meta.tile;
      server.oam[base + 1] = x & 0xff;
      server.oam[base + 2] = y & 0xff;
      server.oam[base + 3] = meta.palette & 0x0f;
      server.oam[base + 4] = 0;
    }
  }

  function updateStreamingColumn(frame, updates) {
    const col = (Math.floor(server.control.scrollX / TILE_SIZE) + 44) % WORLD_COLS;
    const tileBytes = new Uint8Array(WORLD_ROWS);
    const attrBytes = new Uint8Array(WORLD_ROWS);

    for (let row = 0; row < WORLD_ROWS; row += 1) {
      const idx = row * WORLD_COLS + col;
      const tile = scenicWorldCell(col, row);
      const ripple = row >= 30 && (frame / 8 + row + col) % 4 === 0;
      const flower = row >= 18 && row < 22 && (frame / 8 + row * 2 + col) % 17 === 0;
      server.worldTiles[idx] = ripple ? 17 : flower ? 12 : tile.id;
      server.worldAttrs[idx] = (server.worldAttrs[idx] & 0xf0) | (ripple ? 5 : flower ? 7 : tile.palette);
      tileBytes[row] = server.worldTiles[idx];
      attrBytes[row] = server.worldAttrs[idx];
    }

    updates.push(makeColumnUpdate("bg", col, tileBytes, attrBytes));
  }

  function updatePaletteAnimation(frame, updates) {
    const pal = 6;
    const color = 6;
    const offset = (pal * 8 + color) * 3;
    const t = frame / 12;
    server.palettes[offset] = 120 + Math.round(Math.sin(t) * 60);
    server.palettes[offset + 1] = 200 + Math.round(Math.sin(t + 2) * 35);
    server.palettes[offset + 2] = 170 + Math.round(Math.sin(t + 4) * 50);
    updates.push(makePaletteUpdate(pal));
  }

  function updateOverlayCounter(frame, updates) {
    const text = `F${String(frame).padStart(4, "0")}`;
    const start = 25 + 1 * OVERLAY_COLS;
    const beforeTiles = server.overlayTiles.slice(start, start + text.length);
    writeOverlayText(server, 25, 1, text, 3);
    const afterTiles = server.overlayTiles.slice(start, start + text.length);
    if (!sameBytes(beforeTiles, afterTiles)) {
      updates.push(makeOverlayRangeUpdate(start, afterTiles, server.overlayAttrs.slice(start, start + text.length)));
    }
  }

  function updateCharacterTiles(frame, updates) {
    const bank = app.altBanks ? 3 : 0;
    const tileStart = 12;
    const changed = [];
    for (let i = 0; i < 8; i += 1) {
      const tile = tileStart + i;
      writeTile(server, bank, tile, (x, y) => {
        const pulse = Math.sin(frame / 12 + x * 0.7 + y + i) > 0 ? 1 : 0;
        return scenicTilePixel(tile, x, y, pulse);
      });
      changed.push(tile);
    }
    updates.push(makeChrUpdate(bank, changed));
  }

  function makeControlUpdate() {
    const control = { ...server.control };
    return {
      kind: "control",
      wireBytes: 24,
      control,
      apply(target) {
        target.control = { ...control };
      },
    };
  }

  function makeSnapshotUpdate() {
    const snapshot = createVideoState();
    copyState(server, snapshot);
    return {
      kind: "snapshot",
      persistent: true,
      wireBytes: SNAPSHOT_PAYLOAD,
      snapshot,
      apply(target) {
        copyState(snapshot, target);
      },
    };
  }

  function makeOamUpdate(startRecord, bytes) {
    const copy = new Uint8Array(bytes);
    return {
      kind: "oam",
      wireBytes: copy.length,
      startRecord,
      bytes: copy,
      apply(target) {
        target.oam.set(copy, startRecord * OAM_RECORD_BYTES);
      },
    };
  }

  function makeColumnUpdate(kind, col, tileBytes, attrBytes) {
    const tiles = new Uint8Array(tileBytes);
    const attrs = new Uint8Array(attrBytes);
    return {
      kind,
      persistent: true,
      wireBytes: tiles.length + attrs.length,
      col,
      tiles,
      attrs,
      apply(target) {
        for (let row = 0; row < WORLD_ROWS; row += 1) {
          const idx = row * WORLD_COLS + col;
          target.worldTiles[idx] = tiles[row];
          target.worldAttrs[idx] = attrs[row];
        }
      },
    };
  }

  function makePaletteUpdate(paletteId) {
    const start = paletteId * 8 * 3;
    const bytes = server.palettes.slice(start, start + 8 * 3);
    return {
      kind: "palette",
      persistent: true,
      wireBytes: 16,
      paletteId,
      bytes,
      apply(target) {
        target.palettes.set(bytes, start);
      },
    };
  }

  function makeOverlayRangeUpdate(start, tiles, attrs) {
    const tileBytes = new Uint8Array(tiles);
    const attrBytes = new Uint8Array(attrs);
    return {
      kind: "overlay",
      persistent: true,
      wireBytes: tileBytes.length + attrBytes.length,
      start,
      tiles: tileBytes,
      attrs: attrBytes,
      apply(target) {
        target.overlayTiles.set(tileBytes, start);
        target.overlayAttrs.set(attrBytes, start);
      },
    };
  }

  function makeChrUpdate(bank, tileIds) {
    const ids = [...tileIds];
    const pixelData = ids.map((tile) => getTilePixels(server, bank, tile));
    return {
      kind: "chr",
      persistent: true,
      wireBytes: ids.length * CHR_WIRE_BYTES,
      bank,
      tileIds: ids,
      pixelData,
      apply(target) {
        ids.forEach((tile, i) => {
          setTilePixels(target, bank, tile, pixelData[i]);
        });
      },
    };
  }

  function cloneUpdate(update) {
    if (update.kind === "control") {
      const control = { ...update.control };
      return {
        kind: "control",
        wireBytes: update.wireBytes,
        control,
        apply(target) {
          target.control = { ...control };
        },
      };
    }
    if (update.kind === "snapshot") {
      const snapshot = createVideoState();
      copyState(update.snapshot, snapshot);
      return {
        kind: "snapshot",
        persistent: true,
        wireBytes: update.wireBytes,
        snapshot,
        apply(target) {
          copyState(snapshot, target);
        },
      };
    }
    if (update.kind === "oam") {
      return makeOamUpdate(update.startRecord, update.bytes);
    }
    if (update.kind === "bg") {
      return makeColumnUpdate(update.kind, update.col, update.tiles, update.attrs);
    }
    if (update.kind === "palette") {
      const bytes = new Uint8Array(update.bytes);
      const start = update.paletteId * 8 * 3;
      return {
        kind: "palette",
        persistent: true,
        wireBytes: update.wireBytes,
        paletteId: update.paletteId,
        bytes,
        apply(target) {
          target.palettes.set(bytes, start);
        },
      };
    }
    if (update.kind === "overlay") {
      return makeOverlayRangeUpdate(update.start, update.tiles, update.attrs);
    }
    if (update.kind === "chr") {
      const ids = [...update.tileIds];
      const pixelData = update.pixelData.map((data) => new Uint8Array(data));
      return {
        kind: "chr",
        persistent: true,
        wireBytes: update.wireBytes,
        bank: update.bank,
        tileIds: ids,
        pixelData,
        apply(target) {
          ids.forEach((tile, i) => {
            setTilePixels(target, update.bank, tile, pixelData[i]);
          });
        },
      };
    }
    return update;
  }

  function drawState(state, rgba) {
    rgba.fill(255);
    fillBackground(state, rgba);
    drawSprites(state, rgba);
    drawOverlay(state, rgba);
  }

  function fillBackground(state, rgba) {
    const scrollX = state.control.scrollX;
    const scrollY = state.control.scrollY;
    const coarseX = Math.floor(scrollX / TILE_SIZE);
    const coarseY = Math.floor(scrollY / TILE_SIZE);
    const fineX = scrollX & 7;
    const fineY = scrollY & 7;
    const cols = Math.ceil((WIDTH + fineX) / TILE_SIZE) + 1;
    const rows = Math.ceil((HEIGHT + fineY) / TILE_SIZE) + 1;

    for (let ty = 0; ty < rows; ty += 1) {
      const row = positiveMod(coarseY + ty, WORLD_ROWS);
      for (let tx = 0; tx < cols; tx += 1) {
        const col = positiveMod(coarseX + tx, WORLD_COLS);
        const idx = row * WORLD_COLS + col;
        const attr = state.worldAttrs[idx];
        const bank = attr & 0x80 ? state.control.bgAltBank : state.control.bgBank;
        drawTile(state, rgba, bank, state.worldTiles[idx], attr, tx * 8 - fineX, ty * 8 - fineY, false);
      }
    }
  }

  function drawOverlay(state, rgba) {
    for (let row = 0; row < OVERLAY_ROWS; row += 1) {
      for (let col = 0; col < OVERLAY_COLS; col += 1) {
        const idx = row * OVERLAY_COLS + col;
        const attr = state.overlayAttrs[idx];
        const bank = attr & 0x80 ? state.control.overlayAltBank : state.control.overlayBank;
        drawTile(state, rgba, bank, state.overlayTiles[idx], attr, col * 8, row * 8, true);
      }
    }
  }

  function drawSprites(state, rgba) {
    for (let i = 0; i < OAM_COUNT; i += 1) {
      const base = i * OAM_RECORD_BYTES;
      if (state.oam[base + 4] & 0x08) {
        continue;
      }
      const tile = state.oam[base];
      const x = state.oam[base + 1];
      const y = state.oam[base + 2];
      const attr = state.oam[base + 3];
      drawTile(state, rgba, state.control.spriteBank, tile, attr, x, y, true);
    }
  }

  function drawTile(state, rgba, bank, tile, attr, screenX, screenY, transparentZero) {
    const pal = attr & 0x0f;
    const flipX = (attr & 0x10) !== 0;
    const flipY = (attr & 0x20) !== 0;
    const tileBase = tileOffset(bank, tile);

    for (let py = 0; py < TILE_SIZE; py += 1) {
      const y = screenY + py;
      if (y < 0 || y >= HEIGHT) {
        continue;
      }
      const sy = flipY ? 7 - py : py;
      for (let px = 0; px < TILE_SIZE; px += 1) {
        const x = screenX + px;
        if (x < 0 || x >= WIDTH) {
          continue;
        }
        const sx = flipX ? 7 - px : px;
        const colorIndex = state.chars[tileBase + sy * 8 + sx] & 0x07;
        if (transparentZero && colorIndex === 0) {
          continue;
        }
        const colorBase = (pal * 8 + colorIndex) * 3;
        const out = (y * WIDTH + x) * 4;
        rgba[out] = state.palettes[colorBase];
        rgba[out + 1] = state.palettes[colorBase + 1];
        rgba[out + 2] = state.palettes[colorBase + 2];
        rgba[out + 3] = 255;
      }
    }
  }

  function patternPixel(bank, tile, x, y) {
    if (tile === 0) {
      return 0;
    }
    return scenicTilePixel(tile, x, y, bank & 1);
  }

  function scenicTilePixel(tile, x, y, variant) {
    switch (tile) {
      case 1:
        return 3;
      case 2:
        return y > 1 && x > 0 && x < 7 ? 5 : 3;
      case 3:
        return (x > 1 && x < 6 && y < 6) || (x > 3 && y > 3) ? 5 : 3;
      case 4:
        return y >= x / 2 && y >= (7 - x) / 2 ? 2 + ((x + y) & 1) : 3;
      case 5:
        return y < 2 && x > 2 && x < 5 ? 5 : scenicTilePixel(4, x, y, variant);
      case 6:
        return y < 5 && Math.abs(x - 3.5) < 4 - y * 0.45 ? 3 + ((x + y) & 1) : 0;
      case 7:
        return x === 3 || x === 4 ? 2 : 0;
      case 8:
        return y < 2 ? 4 + ((x + variant) & 1) : 2 + ((x + y) & 1);
      case 9:
        return y === 0 ? 5 : y < 3 ? 4 : 2 + ((x * 3 + y) & 1);
      case 10:
        return 2 + ((x + y) & 1);
      case 11:
        return (x === 1 && y === 5) || (x === 6 && y === 2) ? 5 : 2 + ((x + y) & 1);
      case 12:
        return (x === 3 && y > 2) || (y === 3 && x > 1 && x < 6) ? 6 + variant : y < 2 ? 4 : 2;
      case 13:
        return y < 2 ? 3 : 2 + (((x * 2 + y) >> 1) & 1);
      case 14:
        return (x + y) % 5 === 0 ? 5 : scenicTilePixel(13, x, y, variant);
      case 15:
        return y < 2 ? 5 : 2;
      case 16:
        return 2 + (((x + y + variant) >> 1) & 1);
      case 17:
        return (x + y + variant) % 5 === 0 ? 5 : scenicTilePixel(16, x, y, variant);
      case 18:
        return x === 0 || y === 0 ? 5 : 2 + ((x + y) & 1);
      case 19:
        return (x === y && x > 1) || (x === 6 && y < 5) ? 5 : 2 + ((x + y) & 1);
      case 20:
        return x === 3 || x === 4 ? 4 : y > 4 ? 2 : 0;
      case 21:
        return y === 2 || y === 5 ? 5 : 2;
      case 22:
        return y < 3 ? 6 + ((x + y) & 1) : 0;
      case 23:
        return 2 + (((x + y) >> 1) & 1);
      case 24:
        return x > 1 && x < 6 && y > 1 && y < 6 ? 5 : 2;
      case 25:
        return y === 1 || y === 5 || x === 1 || x === 6 ? 5 : 2;
      default:
        return 2 + ((x + y + tile) % 4);
    }
  }

  function writeSpriteTiles(state, bank) {
    writeTile(state, bank, 32, (x, y) => {
      const dx = x - 3.5;
      const dy = y - 3.5;
      return dx * dx + dy * dy < 13 ? 5 + ((x + y) & 1) : 0;
    });
    writeTile(state, bank, 33, (x, y) => (x === 3 || x === 4 || y === 3 || y === 4 ? 6 : x > 1 && x < 6 && y > 1 && y < 6 ? 3 : 0));
    writeTile(state, bank, 34, (x, y) => (y >= x - 1 && y >= 6 - x && y < 7 ? 7 - (y & 1) : 0));
    writeTile(state, bank, 35, (x, y) => (x > 0 && x < 7 && y > 0 && y < 7 ? (x + y) % 3 === 0 ? 7 : 4 : 0));
    writeTile(state, bank, 36, (x, y) => (x === 1 || x === 6 || y === 1 || y === 6 ? 5 : x > 1 && x < 6 && y > 1 && y < 6 ? 2 : 0));
    writeTile(state, bank, 37, (x, y) => ((x + y > 2 && x + y < 12 && x - y < 5 && y - x < 5) ? 6 : 0));
  }

  function writeOverlayGlyphs(state, bank) {
    for (let code = 32; code < 128; code += 1) {
      writeTile(state, bank, code, (x, y) => glyphPixel(String.fromCharCode(code), x, y));
    }
    writeTile(state, bank, 96, (x, y) => (y === 3 || y === 4 ? 4 : 0));
    writeTile(state, bank, 97, (x, y) => (x === 3 || x === 4 ? 4 : 0));
  }

  function glyphPixel(char, x, y) {
    const rows = fontRows(char);
    if (!rows || y >= 7 || x === 0 || x > 5) {
      return 0;
    }
    const bit = (rows[y] >> (5 - x)) & 1;
    return bit ? 5 : 0;
  }

  function writeOverlayText(state, col, row, text, palette) {
    for (let i = 0; i < text.length; i += 1) {
      setOverlayCell(state, col + i, row, text.charCodeAt(i), palette);
    }
  }

  function setOverlayCell(state, col, row, tile, palette) {
    if (col < 0 || col >= OVERLAY_COLS || row < 0 || row >= OVERLAY_ROWS) {
      return;
    }
    const idx = row * OVERLAY_COLS + col;
    state.overlayTiles[idx] = tile;
    state.overlayAttrs[idx] = palette & 0x0f;
  }

  function writeTile(state, bank, tile, fn) {
    const base = tileOffset(bank, tile);
    for (let y = 0; y < 8; y += 1) {
      for (let x = 0; x < 8; x += 1) {
        state.chars[base + y * 8 + x] = fn(x, y) & 0x07;
      }
    }
  }

  function getTilePixels(state, bank, tile) {
    return state.chars.slice(tileOffset(bank, tile), tileOffset(bank, tile) + TILE_PIXELS);
  }

  function setTilePixels(state, bank, tile, pixels) {
    state.chars.set(pixels, tileOffset(bank, tile));
  }

  function tileOffset(bank, tile) {
    return (bank * TILES_PER_BANK + tile) * TILE_PIXELS;
  }

  function copyState(source, target) {
    target.control = { ...source.control };
    target.palettes.set(source.palettes);
    target.chars.set(source.chars);
    target.worldTiles.set(source.worldTiles);
    target.worldAttrs.set(source.worldAttrs);
    target.overlayTiles.set(source.overlayTiles);
    target.overlayAttrs.set(source.overlayAttrs);
    target.oam.set(source.oam);
  }

  function countPixelDiff(a, b) {
    let diff = 0;
    for (let i = 0; i < a.length; i += 4) {
      if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) {
        diff += 1;
      }
    }
    return diff;
  }

  function buildPacketVisuals(items) {
    const sliced = items.slice(-MAX_PACKET_HISTORY);
    return sliced.map((item) => {
      const el = document.createElement("span");
      const kind = item.kind === "snapshot" ? "control" : item.kind;
      el.className = `packet ${kind}${item.repair ? " repair" : ""}${item.dropped ? " dropped" : ""}`;
      return el;
    });
  }

  function summarizeRecent(kinds) {
    if (kinds.length === 0) {
      return "none";
    }
    const counts = new Map();
    for (const kind of kinds) {
      counts.set(kind, (counts.get(kind) || 0) + 1);
    }
    return [...counts.entries()].map(([kind, count]) => `${kind}:${count}`).join(" ");
  }

  function sameBytes(a, b) {
    if (a.length !== b.length) {
      return false;
    }
    for (let i = 0; i < a.length; i += 1) {
      if (a[i] !== b[i]) {
        return false;
      }
    }
    return true;
  }

  function hexToRgb(hex) {
    const value = Number.parseInt(hex.slice(1), 16);
    return [(value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff];
  }

  function positiveMod(value, modulo) {
    return ((value % modulo) + modulo) % modulo;
  }

  function formatBytes(bytes) {
    if (bytes < 1024) {
      return `${bytes} B`;
    }
    return `${(bytes / 1024).toFixed(1)} KiB`;
  }

  function formatKiB(bytes) {
    return `${(bytes / 1024).toFixed(1)} KiB`;
  }

  function fontRows(char) {
    const font = {
      " ": [0, 0, 0, 0, 0, 0, 0],
      "?": [14, 17, 1, 2, 4, 0, 4],
      "0": [14, 17, 19, 21, 25, 17, 14],
      "1": [4, 12, 4, 4, 4, 4, 14],
      "2": [14, 17, 1, 2, 4, 8, 31],
      "3": [30, 1, 1, 14, 1, 1, 30],
      "4": [2, 6, 10, 18, 31, 2, 2],
      "5": [31, 16, 30, 1, 1, 17, 14],
      "6": [6, 8, 16, 30, 17, 17, 14],
      "7": [31, 1, 2, 4, 8, 8, 8],
      "8": [14, 17, 17, 14, 17, 17, 14],
      "9": [14, 17, 17, 15, 1, 2, 12],
      "A": [14, 17, 17, 31, 17, 17, 17],
      "B": [30, 17, 17, 30, 17, 17, 30],
      "C": [14, 17, 16, 16, 16, 17, 14],
      "D": [30, 17, 17, 17, 17, 17, 30],
      "E": [31, 16, 16, 30, 16, 16, 31],
      "F": [31, 16, 16, 30, 16, 16, 16],
      "G": [14, 17, 16, 23, 17, 17, 15],
      "H": [17, 17, 17, 31, 17, 17, 17],
      "I": [14, 4, 4, 4, 4, 4, 14],
      "J": [1, 1, 1, 1, 17, 17, 14],
      "K": [17, 18, 20, 24, 20, 18, 17],
      "L": [16, 16, 16, 16, 16, 16, 31],
      "M": [17, 27, 21, 21, 17, 17, 17],
      "N": [17, 25, 21, 19, 17, 17, 17],
      "O": [14, 17, 17, 17, 17, 17, 14],
      "P": [30, 17, 17, 30, 16, 16, 16],
      "Q": [14, 17, 17, 17, 21, 18, 13],
      "R": [30, 17, 17, 30, 20, 18, 17],
      "S": [15, 16, 16, 14, 1, 1, 30],
      "T": [31, 4, 4, 4, 4, 4, 4],
      "U": [17, 17, 17, 17, 17, 17, 14],
      "V": [17, 17, 17, 17, 17, 10, 4],
      "W": [17, 17, 17, 21, 21, 21, 10],
      "X": [17, 17, 10, 4, 10, 17, 17],
      "Y": [17, 17, 10, 4, 4, 4, 4],
      "Z": [31, 1, 2, 4, 8, 16, 31],
    };
    return font[char] || font["?"];
  }
})();
