(() => {
  "use strict";

  const WIDTH = 320;
  const HEIGHT = 200;
  const TILE_SIZE = 8;
  const CELLS_X = 40;
  const CELLS_Y = 25;
  const WORLD_COLS = 80;
  const WORLD_ROWS = 50;

  const VIDEO_SIZE = 68944;
  const PAGE_SIZE = 32;
  const PAGE_SHIFT = 5;
  const PAGE_COUNT = 2155;
  const DIRTY_MAP_SIZE = 270;
  const HEADER_SIZE = 32;
  const PAGE_RECORD_SIZE = 34;
  const MAX_PAYLOAD = 512;
  const RECORDS_PER_CHUNK = Math.floor((MAX_PAYLOAD - HEADER_SIZE) / PAGE_RECORD_SIZE);
  const DEFAULT_PACKET_LATENCY_MS = 14;
  const CPU_FRAME_MS = 1000 / 30;
  const SIM_STEP_MS = 2;
  const MAX_SIM_CATCHUP_MS = 250;

  const CONTROL = 0x00000;
  const PALETTE = 0x00100;
  const CHR = 0x00200;
  const BG_NT = 0x0c200;
  const BG_ATTR = 0x0e140;
  const OV_NT = 0x10080;
  const OV_ATTR = 0x10468;
  const OAM = 0x10850;

  const CTRL_VIDEO_MODE = CONTROL + 0x01;
  const CTRL_LAYER_ENABLE = CONTROL + 0x03;
  const CTRL_FRAME_ID = CONTROL + 0x04;
  const CTRL_SCROLL_X = CONTROL + 0x08;
  const CTRL_SCROLL_Y = CONTROL + 0x0a;
  const CTRL_BG_ACTIVE_SET = CONTROL + 0x0c;
  const CTRL_BG_SCROLL_MODE = CONTROL + 0x0d;
  const CTRL_BG_CHR_BANK = CONTROL + 0x0e;
  const CTRL_BG_ALT_CHR_BANK = CONTROL + 0x0f;
  const CTRL_OVERLAY_CHR_BANK = CONTROL + 0x10;
  const CTRL_OVERLAY_ALT_CHR_BANK = CONTROL + 0x11;
  const CTRL_SPRITE_CHR_BANK = CONTROL + 0x12;
  const CTRL_BACKDROP = CONTROL + 0x13;
  const CTRL_OAM_COUNT = CONTROL + 0x14;

  const STATUS_NO_DIRTY_PAGES = "NO_DIRTY_PAGES";
  const STATUS_RESPONSE_PENDING = "RESPONSE_PENDING";
  const STATUS_RESPONSE_RESENT = "RESPONSE_RESENT";
  const STATUS_PROTOCOL_ERROR = "PROTOCOL_ERROR";

  const dom = {
    miaCanvas: document.getElementById("mia-canvas"),
    clientCanvas: document.getElementById("client-canvas"),
    playToggle: document.getElementById("play-toggle"),
    fullRefreshButton: document.getElementById("full-refresh-button"),
    dropAckButton: document.getElementById("drop-ack-button"),
    fpsSlider: document.getElementById("fps-slider"),
    fpsOutput: document.getElementById("fps-output"),
    lossSlider: document.getElementById("loss-slider"),
    lossOutput: document.getElementById("loss-output"),
    latencySlider: document.getElementById("latency-slider"),
    latencyOutput: document.getElementById("latency-output"),
    bandwidthSlider: document.getElementById("bandwidth-slider"),
    bandwidthOutput: document.getElementById("bandwidth-output"),
    repairSlider: document.getElementById("repair-slider"),
    repairOutput: document.getElementById("repair-output"),
    miaFpsOutput: document.getElementById("mia-fps-output"),
    miaDrawFpsOutput: document.getElementById("mia-draw-fps-output"),
    clientFpsOutput: document.getElementById("client-fps-output"),
    clientDrawFpsOutput: document.getElementById("client-draw-fps-output"),
    metricFrame: document.getElementById("metric-frame"),
    metricClientFrame: document.getElementById("metric-client-frame"),
    metricActiveDirty: document.getElementById("metric-active-dirty"),
    metricPendingPages: document.getElementById("metric-pending-pages"),
    metricChunks: document.getElementById("metric-chunks"),
    metricPayload: document.getElementById("metric-payload"),
    metricDrops: document.getElementById("metric-drops"),
    metricNacks: document.getElementById("metric-nacks"),
    metricAcks: document.getElementById("metric-acks"),
    metricImplicitAcks: document.getElementById("metric-implicit-acks"),
    metricFullRefreshes: document.getElementById("metric-full-refreshes"),
    metricRenderFps: document.getElementById("metric-render-fps"),
    metricDiff: document.getElementById("metric-diff"),
    metricBottleneck: document.getElementById("metric-bottleneck"),
    metricStatus: document.getElementById("metric-status"),
    metricRecent: document.getElementById("metric-recent"),
    packetStrip: document.getElementById("packet-strip"),
  };

  const miaCtx = dom.miaCanvas.getContext("2d");
  const clientCtx = dom.clientCanvas.getContext("2d");
  const miaImage = miaCtx.createImageData(WIDTH, HEIGHT);
  const clientImage = clientCtx.createImageData(WIDTH, HEIGHT);

  const app = {
    running: true,
    writeMode: "free",
    targetFps: Number(dom.fpsSlider.value),
    packetLoss: Number(dom.lossSlider.value) / 100,
    packetLatencyMs: Number(dom.latencySlider.value || DEFAULT_PACKET_LATENCY_MS),
    bandwidthKiB: Number(dom.bandwidthSlider.value),
    repairTimeoutMs: Number(dom.repairSlider.value),
    dropNextAck: false,
    now: 0,
    lastSimAt: null,
    nextCpuFrameAt: 0,
    cpuFrame: 0,
    packets: [],
    packetHistory: [],
    lastStatus: "WELCOME",
    lastChunks: 0,
    lastPayload: 0,
    drops: 0,
    nacks: 0,
    acks: 0,
    implicitAcks: 0,
    fullRefreshes: 0,
    drawFrames: 0,
    displayedMiaUpdateFps: 0,
    displayedClientApplyFps: 0,
    displayedDrawFps: 0,
    fpsWindowStart: 0,
    fpsWindowCpuFrame: 0,
    fpsWindowClientFrame: 0,
    fpsWindowDrawFrames: 0,
  };

  const mia = {
    ram: new Uint8Array(VIDEO_SIZE),
    dirtyMaps: [new Uint8Array(DIRTY_MAP_SIZE), new Uint8Array(DIRTY_MAP_SIZE)],
    activeMap: 0,
    pendingMap: 1,
    pendingResponse: null,
    fullRefreshPending: true,
    frameId: 0,
    clientFrameId: 0,
    downlinkNextFreeAt: 0,
  };

  const client = {
    mirror: new Uint8Array(VIDEO_SIZE),
    lastCompleteFrameId: 0,
    nextRequestId: 1,
    nextRequestAt: 0,
    pending: null,
  };

  const spriteMeta = Array.from({ length: 18 }, (_, i) => ({
    xBase: 24 + ((i * 37) % 248),
    yBase: 34 + ((i * 29) % 128),
    phase: i * 0.63,
    tile: 48 + (i % 6),
    palette: 5 + (i % 6),
  }));

  initializeVideoRam();
  bindControls();
  appendPacket("status", false, "WELCOME");
  requestAnimationFrame(loop);

  function bindControls() {
    dom.playToggle.addEventListener("click", () => {
      app.running = !app.running;
      dom.playToggle.textContent = app.running ? "II" : ">";
      dom.playToggle.title = app.running ? "Pause simulation" : "Resume simulation";
      dom.playToggle.setAttribute("aria-label", dom.playToggle.title);
    });

    dom.fullRefreshButton.addEventListener("click", () => {
      forceFullRefresh();
    });

    dom.dropAckButton.addEventListener("click", () => {
      app.dropNextAck = !app.dropNextAck;
      dom.dropAckButton.classList.toggle("is-armed", app.dropNextAck);
      setStatus(app.dropNextAck ? "next ACK will be lost" : "ACK loss disarmed");
    });

    dom.fpsSlider.addEventListener("input", () => {
      app.targetFps = Number(dom.fpsSlider.value);
      dom.fpsOutput.textContent = String(app.targetFps);
    });

    dom.lossSlider.addEventListener("input", () => {
      app.packetLoss = Number(dom.lossSlider.value) / 100;
      dom.lossOutput.textContent = `${dom.lossSlider.value}%`;
    });

    dom.latencySlider.addEventListener("input", () => {
      app.packetLatencyMs = Number(dom.latencySlider.value);
      dom.latencyOutput.textContent = `${app.packetLatencyMs} ms`;
    });

    dom.bandwidthSlider.addEventListener("input", () => {
      app.bandwidthKiB = Number(dom.bandwidthSlider.value);
      dom.bandwidthOutput.textContent = app.bandwidthKiB === 0 ? "unlimited" : `${app.bandwidthKiB} KiB/s`;
    });

    dom.repairSlider.addEventListener("input", () => {
      app.repairTimeoutMs = Number(dom.repairSlider.value);
      dom.repairOutput.textContent = `${app.repairTimeoutMs} ms`;
    });

    document.querySelectorAll('input[name="write-mode"]').forEach((input) => {
      input.addEventListener("change", () => {
        if (input.checked) {
          app.writeMode = input.value;
          setStatus(app.writeMode === "ack" ? "6502 waits for ACK clean point" : "6502 writes freely");
        }
      });
    });
  }

  function loop(now) {
    if (app.running) {
      runSimulationUntil(now);
    } else {
      app.now = now;
      app.lastSimAt = now;
    }

    app.now = now;
    drawFrame(mia.ram, miaImage.data);
    drawFrame(client.mirror, clientImage.data);
    miaCtx.putImageData(miaImage, 0, 0);
    clientCtx.putImageData(clientImage, 0, 0);
    app.drawFrames += 1;
    updateMetrics();
    requestAnimationFrame(loop);
  }

  function runSimulationUntil(now) {
    if (app.lastSimAt === null) {
      app.lastSimAt = now;
      app.nextCpuFrameAt = now;
      client.nextRequestAt = now;
      return;
    }

    const targetNow = Math.min(now, app.lastSimAt + MAX_SIM_CATCHUP_MS);
    while (app.lastSimAt < targetNow) {
      const simNow = Math.min(app.lastSimAt + SIM_STEP_MS, targetNow);
      stepSimulation(simNow);
      app.lastSimAt = simNow;
    }

    if (targetNow < now) {
      app.lastSimAt = now;
      app.nextCpuFrameAt = Math.max(app.nextCpuFrameAt, now);
      client.nextRequestAt = Math.max(client.nextRequestAt, now);
    }
  }

  function stepSimulation(now) {
    app.now = now;
    processPacketQueue(now);

    while (now >= app.nextCpuFrameAt) {
      if (app.writeMode === "free" || !mia.pendingResponse) {
        advance6502Frame();
      }
      app.nextCpuFrameAt += CPU_FRAME_MS;
    }

    clientService(now);
  }

  function clientService(now) {
    if (client.pending) {
      handleClientTimeout(now);
      return;
    }

    if (now < client.nextRequestAt) {
      return;
    }

    const requestId = client.nextRequestId;
    client.nextRequestId = (client.nextRequestId + 1) & 0xffff;
    startClientRequest(requestId, client.lastCompleteFrameId);
    handleRequestFrame(requestId, client.lastCompleteFrameId);
    client.nextRequestAt = now + 1000 / app.targetFps;
  }

  function startClientRequest(requestId, lastCompleteFrameId) {
    client.pending = {
      requestId,
      lastCompleteFrameId,
      frameId: 0,
      chunkCount: 0,
      chunks: new Map(),
      deadline: repairDeadline(app.now),
    };
    appendPacket("status", false, "REQUEST_FRAME");
  }

  function handleClientTimeout(now) {
    const pending = client.pending;
    if (!pending || now < pending.deadline) {
      return;
    }

    if (pending.frameId === 0) {
      pending.deadline = repairDeadline(now);
      appendPacket("status", false, "REQUEST retry");
      handleRequestFrame(pending.requestId, pending.lastCompleteFrameId);
      return;
    }

    const missing = [];
    for (let i = 0; i < pending.chunkCount; i += 1) {
      if (!pending.chunks.has(i)) {
        missing.push(i);
      }
    }

    if (missing.length > 0) {
      app.nacks += 1;
      pending.deadline = repairDeadline(now);
      appendPacket("nack", false, `NACK ${missing.length}`);
      handleNackChunks(pending.requestId, pending.frameId, missing);
    }
  }

  function handleRequestFrame(requestId, lastCompleteFrameId) {
    const pending = mia.pendingResponse;

    if (pending) {
      if (requestId === pending.requestId && lastCompleteFrameId === pending.baseClientFrameId) {
        setStatus(STATUS_RESPONSE_RESENT);
        scheduleChunks(pending, allChunkIndexes(pending), true);
        return;
      }

      if (lastCompleteFrameId === pending.frameId) {
        app.implicitAcks += 1;
        appendPacket("ack", false, "implicit ACK");
        clearPendingResponse(pending.frameId);
        handleRequestFrame(requestId, lastCompleteFrameId);
        return;
      }

      if (serialNewer(lastCompleteFrameId, pending.frameId)) {
        protocolError();
        receiveStatus(STATUS_PROTOCOL_ERROR);
        return;
      }

      receiveStatus(STATUS_RESPONSE_PENDING);
      return;
    }

    if (lastCompleteFrameId < mia.clientFrameId) {
      setStatus("stale REQUEST ignored");
      client.pending = null;
      return;
    }

    if (serialNewer(lastCompleteFrameId, mia.clientFrameId)) {
      protocolError();
      receiveStatus(STATUS_PROTOCOL_ERROR);
      return;
    }

    const activeDirty = countDirtyPages(mia.dirtyMaps[mia.activeMap]);
    if (!mia.fullRefreshPending && activeDirty === 0) {
      receiveStatus(STATUS_NO_DIRTY_PAGES);
      return;
    }

    const oldActive = mia.activeMap;
    const newActive = oldActive ^ 1;
    mia.activeMap = newActive;
    mia.pendingMap = oldActive;

    if (mia.fullRefreshPending) {
      markAllPendingPagesDirty();
      mia.fullRefreshPending = false;
      app.fullRefreshes += 1;
    }

    const pages = scanDirtyMap(mia.dirtyMaps[mia.pendingMap]);
    if (pages.length === 0) {
      receiveStatus(STATUS_NO_DIRTY_PAGES);
      return;
    }

    mia.frameId = nextFrameId(mia.frameId);
    writeU32(CTRL_FRAME_ID, mia.frameId);

    const response = {
      requestId,
      baseClientFrameId: lastCompleteFrameId,
      frameId: mia.frameId,
      pages,
      chunkCount: Math.ceil(pages.length / RECORDS_PER_CHUNK),
    };

    mia.pendingResponse = response;
    app.lastChunks = response.chunkCount;
    app.lastPayload = pages.length * PAGE_RECORD_SIZE;
    setStatus(`FRAME_DATA ${response.chunkCount} chunks`);
    scheduleChunks(response, allChunkIndexes(response), false);
  }

  function handleAckResponse(requestId, frameId) {
    const pending = mia.pendingResponse;
    appendPacket("ack", false, "ACK_RESPONSE");

    if (pending && requestId === pending.requestId && frameId === pending.frameId) {
      app.acks += 1;
      clearPendingResponse(frameId);
      setStatus("FRAME_ACKED");
      return;
    }

    if (pending && serialNewer(frameId, pending.frameId)) {
      protocolError();
    }
  }

  function handleNackChunks(requestId, frameId, missingIndexes) {
    const pending = mia.pendingResponse;

    if (!pending || requestId !== pending.requestId || frameId !== pending.frameId) {
      if (pending && serialNewer(frameId, pending.frameId)) {
        protocolError();
      }
      return;
    }

    if (missingIndexes.some((index) => index < 0 || index >= pending.chunkCount)) {
      protocolError();
      return;
    }

    setStatus(`repair ${missingIndexes.length} chunks`);
    scheduleChunks(pending, missingIndexes, true);
  }

  function receiveFrameData(packet) {
    const pending = client.pending;
    if (!pending || pending.requestId !== packet.requestId) {
      return;
    }

    if (pending.frameId === 0) {
      pending.frameId = packet.frameId;
      pending.chunkCount = packet.chunkCount;
    }

    if (packet.frameId !== pending.frameId || packet.chunkCount !== pending.chunkCount) {
      client.pending = null;
      protocolError();
      return;
    }

    pending.chunks.set(packet.chunkIndex, packet.records);
    pending.deadline = repairDeadline(app.now);

    if (pending.chunks.size === pending.chunkCount) {
      applyCompleteResponse(pending);
    }
  }

  function receiveStatus(code) {
    appendPacket("status", code === STATUS_PROTOCOL_ERROR, code);
    setStatus(code);
    if (code === STATUS_NO_DIRTY_PAGES || code === STATUS_PROTOCOL_ERROR) {
      client.pending = null;
    }
  }

  function applyCompleteResponse(pending) {
    let lastPage = -1;
    const orderedChunks = [];
    for (let i = 0; i < pending.chunkCount; i += 1) {
      orderedChunks.push(pending.chunks.get(i));
    }

    for (const chunk of orderedChunks) {
      for (const record of chunk) {
        if (record.pageIndex <= lastPage || record.pageIndex >= PAGE_COUNT) {
          client.pending = null;
          protocolError();
          return;
        }
        lastPage = record.pageIndex;
        const offset = record.pageIndex * PAGE_SIZE;
        const validLen = Math.min(PAGE_SIZE, VIDEO_SIZE - offset);
        client.mirror.set(record.data.subarray(0, validLen), offset);
      }
    }

    client.lastCompleteFrameId = pending.frameId;
    client.pending = null;

    if (app.dropNextAck) {
      app.dropNextAck = false;
      dom.dropAckButton.classList.remove("is-armed");
      appendPacket("ack dropped", true, "ACK lost");
      setStatus("ACK lost; next request carries last_complete_frame_id");
      return;
    }

    handleAckResponse(pending.requestId, pending.frameId);
  }

  function scheduleChunks(response, chunkIndexes, repair) {
    for (const chunkIndex of chunkIndexes) {
      const firstRecord = chunkIndex * RECORDS_PER_CHUNK;
      const recordCount = Math.min(RECORDS_PER_CHUNK, response.pages.length - firstRecord);
      if (recordCount <= 0) {
        continue;
      }

      const payloadLen = recordCount * PAGE_RECORD_SIZE;
      const packetBytes = HEADER_SIZE + payloadLen;
      const sendStart = Math.max(app.now, mia.downlinkNextFreeAt);
      const sendMs = app.bandwidthKiB === 0 ? 0 : packetBytes / ((app.bandwidthKiB * 1024) / 1000);
      const sendDone = sendStart + sendMs;
      const dropped = Math.random() < app.packetLoss;

      mia.downlinkNextFreeAt = sendDone;
      app.packets.push({
        deliveryTime: sendDone + app.packetLatencyMs,
        response,
        chunkIndex,
        dropped,
        repair,
      });
    }

    app.packets.sort((a, b) => a.deliveryTime - b.deliveryTime);
  }

  function repairDeadline(fromTime) {
    return fromTime + app.repairTimeoutMs + maxChunkTransmitMs() + app.packetLatencyMs;
  }

  function maxChunkTransmitMs() {
    if (app.bandwidthKiB === 0) {
      return 0;
    }
    const maxPacketBytes = HEADER_SIZE + RECORDS_PER_CHUNK * PAGE_RECORD_SIZE;
    return maxPacketBytes / ((app.bandwidthKiB * 1024) / 1000);
  }

  function processPacketQueue(now) {
    while (app.packets.length > 0 && app.packets[0].deliveryTime <= now) {
      const event = app.packets.shift();
      if (event.dropped) {
        app.drops += 1;
        appendPacket(event.repair ? "repair" : "frame", true, "dropped");
        continue;
      }

      appendPacket(event.repair ? "repair" : "frame", false, event.repair ? "FRAME_DATA repair" : "FRAME_DATA");
      receiveFrameData(buildFrameDataPacket(event.response, event.chunkIndex));
    }
  }

  function buildFrameDataPacket(response, chunkIndex) {
    const firstRecord = chunkIndex * RECORDS_PER_CHUNK;
    const recordCount = Math.min(RECORDS_PER_CHUNK, response.pages.length - firstRecord);
    const records = [];

    for (let i = 0; i < recordCount; i += 1) {
      const pageIndex = response.pages[firstRecord + i];
      const offset = pageIndex * PAGE_SIZE;
      const data = new Uint8Array(PAGE_SIZE);
      const validLen = Math.min(PAGE_SIZE, VIDEO_SIZE - offset);
      data.set(mia.ram.subarray(offset, offset + validLen));
      records.push({ pageIndex, data });
    }

    return {
      requestId: response.requestId,
      frameId: response.frameId,
      chunkIndex,
      chunkCount: response.chunkCount,
      records,
    };
  }

  function clearPendingResponse(frameId) {
    mia.dirtyMaps[mia.pendingMap].fill(0);
    mia.pendingResponse = null;
    mia.clientFrameId = frameId;
  }

  function protocolError() {
    if (mia.pendingResponse) {
      mia.dirtyMaps[mia.pendingMap].fill(0);
    }
    mia.pendingResponse = null;
    mia.fullRefreshPending = true;
    setStatus(STATUS_PROTOCOL_ERROR);
  }

  function forceFullRefresh() {
    mia.fullRefreshPending = true;
    setStatus("FULL_REFRESH_PENDING");
    appendPacket("status", false, "FULL_REFRESH_PENDING");
  }

  function allChunkIndexes(response) {
    return Array.from({ length: response.chunkCount }, (_, i) => i);
  }

  function nextFrameId(frameId) {
    return frameId === 0xffffffff ? 1 : frameId + 1;
  }

  function serialNewer(a, b) {
    const diff = (a - b) >>> 0;
    return diff > 0 && diff < 0x80000000;
  }

  function frameDelta(newer, older) {
    return (newer - older) >>> 0;
  }

  function markDirty(offset) {
    if (offset < 0 || offset >= VIDEO_SIZE) {
      return;
    }
    const page = offset >> PAGE_SHIFT;
    mia.dirtyMaps[mia.activeMap][page >> 3] |= 1 << (page & 7);
  }

  function cpuWrite(offset, value) {
    if (offset < 0 || offset >= VIDEO_SIZE) {
      return;
    }
    mia.ram[offset] = value & 0xff;
    markDirty(offset);
  }

  function writeU16(offset, value) {
    cpuWrite(offset, value & 0xff);
    cpuWrite(offset + 1, (value >> 8) & 0xff);
  }

  function writeU32(offset, value) {
    cpuWrite(offset, value & 0xff);
    cpuWrite(offset + 1, (value >>> 8) & 0xff);
    cpuWrite(offset + 2, (value >>> 16) & 0xff);
    cpuWrite(offset + 3, (value >>> 24) & 0xff);
  }

  function readU16(memory, offset) {
    return memory[offset] | (memory[offset + 1] << 8);
  }

  function countDirtyPages(map) {
    let count = 0;
    for (const byte of map) {
      count += popCount8(byte);
    }
    return count;
  }

  function popCount8(value) {
    value -= (value >> 1) & 0x55;
    value = (value & 0x33) + ((value >> 2) & 0x33);
    return (value + (value >> 4)) & 0x0f;
  }

  function scanDirtyMap(map) {
    const pages = [];
    for (let byteIndex = 0; byteIndex < DIRTY_MAP_SIZE; byteIndex += 1) {
      let bits = map[byteIndex];
      while (bits !== 0) {
        const bit = trailingZero8(bits);
        const page = byteIndex * 8 + bit;
        if (page < PAGE_COUNT) {
          pages.push(page);
        }
        bits &= bits - 1;
      }
    }
    return pages;
  }

  function trailingZero8(value) {
    for (let bit = 0; bit < 8; bit += 1) {
      if ((value & (1 << bit)) !== 0) {
        return bit;
      }
    }
    return 8;
  }

  function markAllPendingPagesDirty() {
    const map = mia.dirtyMaps[mia.pendingMap];
    map.fill(0xff);
    const extraBits = DIRTY_MAP_SIZE * 8 - PAGE_COUNT;
    if (extraBits > 0) {
      map[DIRTY_MAP_SIZE - 1] &= 0xff >>> extraBits;
    }
  }

  function initializeVideoRam() {
    cpuWrite(CONTROL, 1);
    cpuWrite(CTRL_VIDEO_MODE, 1);
    cpuWrite(CTRL_LAYER_ENABLE, 0x07);
    cpuWrite(CTRL_BG_ACTIVE_SET, 0);
    cpuWrite(CTRL_BG_SCROLL_MODE, 3);
    cpuWrite(CTRL_BG_CHR_BANK, 0);
    cpuWrite(CTRL_BG_ALT_CHR_BANK, 3);
    cpuWrite(CTRL_OVERLAY_CHR_BANK, 1);
    cpuWrite(CTRL_OVERLAY_ALT_CHR_BANK, 4);
    cpuWrite(CTRL_SPRITE_CHR_BANK, 2);
    cpuWrite(CTRL_BACKDROP, 0);
    writeU16(CTRL_OAM_COUNT, spriteMeta.length);

    loadPalettes();
    loadCharacterBanks();
    loadWorldTables();
    loadOverlay();
    updateSprites(0);
  }

  function loadPalettes() {
    const palettes = [
      ["#1a2332", "#284c68", "#3f7f80", "#74c69d", "#d9ed92", "#f8f1d9", "#f0a04b", "#c84646"],
      ["#171412", "#46372d", "#7a553d", "#a87554", "#d9a05f", "#ffe0a3", "#7ab87a", "#45705a"],
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
    ];

    palettes.forEach((palette, pal) => {
      palette.forEach((hex, index) => {
        writePaletteColor(pal, index, hexToRgb565(hex));
      });
    });
  }

  function loadCharacterBanks() {
    for (let bank = 0; bank < 8; bank += 1) {
      for (let tile = 0; tile < 256; tile += 1) {
        writeTile(bank, tile, (x, y) => scenicPixel(tile, x, y, bank & 1));
      }
    }
    writeOverlayGlyphs(1);
    writeOverlayGlyphs(4);
    writeSpriteTiles(2);
  }

  function loadWorldTables() {
    for (let row = 0; row < WORLD_ROWS; row += 1) {
      for (let col = 0; col < WORLD_COLS; col += 1) {
        const cell = scenicCell(col, row, 0);
        writeBgCell(col, row, cell.tile, cell.attr);
      }
    }
  }

  function loadOverlay() {
    for (let i = 0; i < 1000; i += 1) {
      cpuWrite(OV_NT + i, 0);
      cpuWrite(OV_ATTR + i, 0x03);
    }

    for (let col = 0; col < CELLS_X; col += 1) {
      writeOverlayCell(col, 0, 96, 3);
      writeOverlayCell(col, CELLS_Y - 1, 96, 3);
    }
    for (let row = 0; row < CELLS_Y; row += 1) {
      writeOverlayCell(0, row, 97, 3);
      writeOverlayCell(CELLS_X - 1, row, 97, 3);
    }
    writeOverlayText(2, 1, "MIA DIRTY PAGE POC", 3);
    writeOverlayText(2, 23, "FRAME 0000", 3);
  }

  function advance6502Frame() {
    app.cpuFrame += 1;
    const scrollX = app.cpuFrame % (WORLD_COLS * TILE_SIZE);
    const scrollY = Math.max(0, Math.round(10 + Math.sin(app.cpuFrame / 70) * 8));
    writeU16(CTRL_SCROLL_X, scrollX);
    writeU16(CTRL_SCROLL_Y, scrollY);
    updateSprites(app.cpuFrame);

    if (app.cpuFrame % 8 === 0) {
      updateStreamingColumn(app.cpuFrame);
    }
    if (app.cpuFrame % 18 === 0) {
      updatePaletteAnimation(app.cpuFrame);
    }
    if (app.cpuFrame % 30 === 0) {
      writeOverlayText(8, 23, String(app.cpuFrame).padStart(4, "0"), 3);
    }
    if (app.cpuFrame % 90 === 0) {
      updateCharacterPulse(app.cpuFrame);
    }
  }

  function updateStreamingColumn(frame) {
    const col = (Math.floor(readU16(mia.ram, CTRL_SCROLL_X) / TILE_SIZE) + 44) % WORLD_COLS;
    for (let row = 0; row < WORLD_ROWS; row += 1) {
      const cell = scenicCell(col, row, frame);
      writeBgCell(col, row, cell.tile, cell.attr);
    }
  }

  function updatePaletteAnimation(frame) {
    const t = frame / 12;
    const red = 110 + Math.round(Math.sin(t) * 56);
    const green = 200 + Math.round(Math.sin(t + 2) * 34);
    const blue = 166 + Math.round(Math.sin(t + 4) * 48);
    writePaletteColor(6, 6, rgb565(red, green, blue));
  }

  function updateCharacterPulse(frame) {
    const bank = frame % 180 === 0 ? 3 : 0;
    for (let tile = 12; tile < 20; tile += 1) {
      writeTile(bank, tile, (x, y) => {
        const pulse = Math.sin(frame / 10 + x * 0.8 + y + tile) > 0 ? 1 : 0;
        return scenicPixel(tile, x, y, pulse);
      });
    }
  }

  function updateSprites(frame) {
    spriteMeta.forEach((meta, i) => {
      const x = Math.round(meta.xBase + Math.sin(frame / 18 + meta.phase) * 24);
      const y = Math.round(meta.yBase + Math.sin(frame / 14 + meta.phase) * 28);
      const base = OAM + i * 5;
      cpuWrite(base, meta.tile);
      cpuWrite(base + 1, x & 0xff);
      cpuWrite(base + 2, y & 0xff);
      cpuWrite(base + 3, meta.palette & 0x0f);
      cpuWrite(base + 4, ((x >> 8) & 0x03) | (((y >> 8) & 0x01) << 2));
    });
  }

  function writeBgCell(col, row, tile, attr) {
    const tableCol = col >= 40 ? 1 : 0;
    const tableRow = row >= 25 ? 1 : 0;
    const table = tableRow * 2 + tableCol;
    const local = (row % 25) * 40 + (col % 40);
    cpuWrite(BG_NT + table * 1000 + local, tile);
    cpuWrite(BG_ATTR + table * 1000 + local, attr);
  }

  function writeOverlayCell(col, row, tile, attr) {
    if (col < 0 || col >= CELLS_X || row < 0 || row >= CELLS_Y) {
      return;
    }
    const offset = row * CELLS_X + col;
    cpuWrite(OV_NT + offset, tile);
    cpuWrite(OV_ATTR + offset, attr);
  }

  function writeOverlayText(col, row, text, palette) {
    for (let i = 0; i < text.length; i += 1) {
      writeOverlayCell(col + i, row, text.charCodeAt(i), palette);
    }
  }

  function writePaletteColor(palette, color, value) {
    const offset = PALETTE + palette * 16 + color * 2;
    cpuWrite(offset, value & 0xff);
    cpuWrite(offset + 1, (value >> 8) & 0xff);
  }

  function writeTile(bank, tile, pixelFn) {
    for (let plane = 0; plane < 3; plane += 1) {
      for (let y = 0; y < 8; y += 1) {
        let byte = 0;
        for (let x = 0; x < 8; x += 1) {
          const color = pixelFn(x, y) & 0x07;
          byte |= ((color >> plane) & 1) << x;
        }
        cpuWrite(CHR + bank * 6144 + plane * 2048 + tile * 8 + y, byte);
      }
    }
  }

  function writeOverlayGlyphs(bank) {
    for (let code = 32; code < 128; code += 1) {
      writeTile(bank, code, (x, y) => glyphPixel(String.fromCharCode(code), x, y));
    }
    writeTile(bank, 96, (x, y) => (y === 3 || y === 4 ? 4 : 0));
    writeTile(bank, 97, (x, y) => (x === 3 || x === 4 ? 4 : 0));
  }

  function writeSpriteTiles(bank) {
    writeTile(bank, 48, (x, y) => ((x - 3.5) ** 2 + (y - 3.5) ** 2 < 13 ? 5 + ((x + y) & 1) : 0));
    writeTile(bank, 49, (x, y) => (x === 3 || x === 4 || y === 3 || y === 4 ? 6 : x > 1 && x < 6 && y > 1 && y < 6 ? 3 : 0));
    writeTile(bank, 50, (x, y) => (y >= x - 1 && y >= 6 - x && y < 7 ? 7 - (y & 1) : 0));
    writeTile(bank, 51, (x, y) => (x > 0 && x < 7 && y > 0 && y < 7 ? ((x + y) % 3 === 0 ? 7 : 4) : 0));
    writeTile(bank, 52, (x, y) => (x === 1 || x === 6 || y === 1 || y === 6 ? 5 : x > 1 && x < 6 && y > 1 && y < 6 ? 2 : 0));
    writeTile(bank, 53, (x, y) => (x + y > 2 && x + y < 12 && x - y < 5 && y - x < 5 ? 6 : 0));
  }

  function scenicCell(col, row, frame) {
    const cloud = row < 5 && ((col > 7 && col < 18 && row > 1) || (col > 49 && col < 64 && row > 0));
    const peak = 7 + Math.abs(((col + 12) % 24) - 12) / 2;
    const farPeak = 9 + Math.abs(((col + 2) % 18) - 9) / 2;
    const house = col >= 54 && col <= 65 && row >= 14 && row <= 21;
    const pondEdge = 30 + Math.floor(Math.sin(col / 5) * 2);
    const ripple = row >= 30 && (Math.floor(frame / 8) + row + col) % 4 === 0;
    const flower = row >= 18 && row < 22 && (Math.floor(frame / 8) + row * 2 + col) % 17 === 0;

    if (house) {
      if (row === 14 || row === 15) {
        return { tile: 22, attr: 8 };
      }
      if ((col === 58 || col === 61) && row >= 17 && row <= 18) {
        return { tile: 24, attr: 8 };
      }
      return { tile: 23, attr: 8 };
    }
    if (cloud) {
      return { tile: row < 3 ? 2 : 3, attr: 2 };
    }
    if (row < peak && row > 5) {
      return { tile: row < peak - 2 ? 5 : 4, attr: 9 };
    }
    if (row < farPeak && row > 7) {
      return { tile: 4, attr: 3 };
    }
    if (row < 13) {
      return { tile: 1, attr: 2 };
    }
    if (row < 16) {
      return { tile: (col + row) % 5 === 0 ? 6 : 1, attr: 7 };
    }
    if (row < 18) {
      return { tile: (col + row) % 7 === 0 ? 7 : 8, attr: 7 };
    }
    if (row < 22) {
      return { tile: flower ? 12 : 8 + ((col + row) % 2), attr: flower ? 7 : 7 };
    }
    if (row < 27) {
      return { tile: (col + row) % 6 === 0 ? 14 : 13, attr: 1 };
    }
    if (row >= pondEdge) {
      return { tile: ripple ? 17 : 16, attr: 5 };
    }
    return { tile: (col + row) % 11 === 0 ? 11 : 10, attr: 1 };
  }

  function scenicPixel(tile, x, y, variant) {
    switch (tile) {
      case 0:
        return 0;
      case 1:
        return 3;
      case 2:
        return y > 1 && x > 0 && x < 7 ? 5 : 3;
      case 3:
        return (x > 1 && x < 6 && y < 6) || (x > 3 && y > 3) ? 5 : 3;
      case 4:
        return y >= x / 2 && y >= (7 - x) / 2 ? 2 + ((x + y) & 1) : 3;
      case 5:
        return y < 2 && x > 2 && x < 5 ? 5 : scenicPixel(4, x, y, variant);
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
        return (x + y) % 5 === 0 ? 5 : scenicPixel(13, x, y, variant);
      case 16:
        return 2 + (((x + y + variant) >> 1) & 1);
      case 17:
        return (x + y + variant) % 5 === 0 ? 5 : scenicPixel(16, x, y, variant);
      case 22:
        return y < 3 ? 6 + ((x + y) & 1) : 0;
      case 23:
        return 2 + (((x + y) >> 1) & 1);
      case 24:
        return x > 1 && x < 6 && y > 1 && y < 6 ? 5 : 2;
      default:
        return 2 + ((x + y + tile) % 4);
    }
  }

  function drawFrame(memory, rgba) {
    const backdrop = readPaletteColor(memory, 0, memory[CTRL_BACKDROP] & 0x07);
    for (let i = 0; i < rgba.length; i += 4) {
      rgba[i] = backdrop[0];
      rgba[i + 1] = backdrop[1];
      rgba[i + 2] = backdrop[2];
      rgba[i + 3] = 255;
    }
    if (memory[CTRL_LAYER_ENABLE] & 0x01) {
      drawBackground(memory, rgba);
    }
    if (memory[CTRL_LAYER_ENABLE] & 0x04) {
      drawSprites(memory, rgba);
    }
    if (memory[CTRL_LAYER_ENABLE] & 0x02) {
      drawOverlay(memory, rgba);
    }
  }

  function drawBackground(memory, rgba) {
    const scrollX = readU16(memory, CTRL_SCROLL_X);
    const scrollY = readU16(memory, CTRL_SCROLL_Y);
    const coarseX = Math.floor(scrollX / TILE_SIZE);
    const coarseY = Math.floor(scrollY / TILE_SIZE);
    const fineX = scrollX & 7;
    const fineY = scrollY & 7;
    const cols = Math.ceil((WIDTH + fineX) / TILE_SIZE) + 1;
    const rows = Math.ceil((HEIGHT + fineY) / TILE_SIZE) + 1;

    for (let ty = 0; ty < rows; ty += 1) {
      const worldRow = positiveMod(coarseY + ty, WORLD_ROWS);
      for (let tx = 0; tx < cols; tx += 1) {
        const worldCol = positiveMod(coarseX + tx, WORLD_COLS);
        const { table, local } = bgTableAndLocal(worldCol, worldRow);
        const tile = memory[BG_NT + table * 1000 + local];
        const attr = memory[BG_ATTR + table * 1000 + local];
        const bank = attr & 0x80 ? memory[CTRL_BG_ALT_CHR_BANK] : memory[CTRL_BG_CHR_BANK];
        drawTile(memory, rgba, bank, tile, attr, tx * 8 - fineX, ty * 8 - fineY, false, false);
      }
    }
  }

  function drawOverlay(memory, rgba) {
    for (let row = 0; row < CELLS_Y; row += 1) {
      for (let col = 0; col < CELLS_X; col += 1) {
        const offset = row * CELLS_X + col;
        const tile = memory[OV_NT + offset];
        const attr = memory[OV_ATTR + offset];
        const bank = attr & 0x80 ? memory[CTRL_OVERLAY_ALT_CHR_BANK] : memory[CTRL_OVERLAY_CHR_BANK];
        drawTile(memory, rgba, bank, tile, attr, col * 8, row * 8, true, false);
      }
    }
  }

  function drawSprites(memory, rgba) {
    const count = Math.min(readU16(memory, CTRL_OAM_COUNT), 256);
    const bank = memory[CTRL_SPRITE_CHR_BANK];
    for (let i = 0; i < count; i += 1) {
      const base = OAM + i * 5;
      const ext = memory[base + 4];
      if (ext & 0x08) {
        continue;
      }
      const x = signExtend(memory[base + 1] | ((ext & 0x03) << 8), 10);
      const y = signExtend(memory[base + 2] | (((ext >> 2) & 0x01) << 8), 9);
      drawTile(memory, rgba, bank, memory[base], memory[base + 3], x, y, true, true);
    }
  }

  function drawTile(memory, rgba, bank, tile, attr, screenX, screenY, transparentZero, spriteAttr) {
    const palette = attr & 0x0f;
    const flipX = spriteAttr ? (attr & 0x20) !== 0 : (attr & 0x10) !== 0;
    const flipY = spriteAttr ? (attr & 0x40) !== 0 : (attr & 0x20) !== 0;

    for (let py = 0; py < 8; py += 1) {
      const y = screenY + py;
      if (y < 0 || y >= HEIGHT) {
        continue;
      }
      const sy = flipY ? 7 - py : py;
      for (let px = 0; px < 8; px += 1) {
        const x = screenX + px;
        if (x < 0 || x >= WIDTH) {
          continue;
        }
        const sx = flipX ? 7 - px : px;
        const colorIndex = readChrPixel(memory, bank & 7, tile, sx, sy);
        if (transparentZero && colorIndex === 0) {
          continue;
        }
        const color = readPaletteColor(memory, palette, colorIndex);
        const out = (y * WIDTH + x) * 4;
        rgba[out] = color[0];
        rgba[out + 1] = color[1];
        rgba[out + 2] = color[2];
        rgba[out + 3] = 255;
      }
    }
  }

  function readChrPixel(memory, bank, tile, x, y) {
    let color = 0;
    for (let plane = 0; plane < 3; plane += 1) {
      const byte = memory[CHR + bank * 6144 + plane * 2048 + tile * 8 + y];
      color |= ((byte >> x) & 1) << plane;
    }
    return color;
  }

  function readPaletteColor(memory, palette, colorIndex) {
    const offset = PALETTE + (palette & 0x0f) * 16 + (colorIndex & 0x07) * 2;
    const value = memory[offset] | (memory[offset + 1] << 8);
    return [
      Math.round(((value >> 11) & 0x1f) * 255 / 31),
      Math.round(((value >> 5) & 0x3f) * 255 / 63),
      Math.round((value & 0x1f) * 255 / 31),
    ];
  }

  function bgTableAndLocal(col, row) {
    const table = (row >= 25 ? 2 : 0) + (col >= 40 ? 1 : 0);
    return {
      table,
      local: (row % 25) * 40 + (col % 40),
    };
  }

  function updateMetrics() {
    updateOutputFps();
    dom.miaFpsOutput.textContent = `RAM ${app.displayedMiaUpdateFps.toFixed(1)} FPS`;
    dom.miaDrawFpsOutput.textContent = `Draw ${app.displayedDrawFps.toFixed(1)} FPS`;
    dom.clientFpsOutput.textContent = `Apply ${app.displayedClientApplyFps.toFixed(1)} FPS`;
    dom.clientDrawFpsOutput.textContent = `Draw ${app.displayedDrawFps.toFixed(1)} FPS`;
    dom.metricFrame.textContent = String(mia.frameId);
    dom.metricClientFrame.textContent = String(client.lastCompleteFrameId);
    dom.metricActiveDirty.textContent = String(countDirtyPages(mia.dirtyMaps[mia.activeMap]));
    dom.metricPendingPages.textContent = String(mia.pendingResponse ? mia.pendingResponse.pages.length : 0);
    dom.metricChunks.textContent = String(app.lastChunks);
    dom.metricPayload.textContent = formatBytes(app.lastPayload);
    dom.metricDrops.textContent = String(app.drops);
    dom.metricNacks.textContent = String(app.nacks);
    dom.metricAcks.textContent = String(app.acks);
    dom.metricImplicitAcks.textContent = String(app.implicitAcks);
    dom.metricFullRefreshes.textContent = String(app.fullRefreshes);
    dom.metricRenderFps.textContent = `${app.displayedDrawFps.toFixed(1)} FPS`;
    dom.metricDiff.textContent = String(countPixelDiff(miaImage.data, clientImage.data));
    dom.metricBottleneck.textContent = diagnoseBottleneck();
    dom.metricStatus.textContent = app.lastStatus;
    dom.metricRecent.textContent = summarizePackets();
    dom.packetStrip.replaceChildren(...packetVisuals());
  }

  function updateOutputFps() {
    if (app.fpsWindowStart === 0) {
      app.fpsWindowStart = app.now;
      app.fpsWindowCpuFrame = app.cpuFrame;
      app.fpsWindowClientFrame = client.lastCompleteFrameId;
      app.fpsWindowDrawFrames = app.drawFrames;
      return;
    }

    const elapsed = app.now - app.fpsWindowStart;
    if (elapsed < 500) {
      return;
    }

    const miaFrames = app.cpuFrame - app.fpsWindowCpuFrame;
    const clientFrames = frameDelta(client.lastCompleteFrameId, app.fpsWindowClientFrame);
    const drawFrames = app.drawFrames - app.fpsWindowDrawFrames;
    app.displayedMiaUpdateFps = miaFrames * 1000 / elapsed;
    app.displayedClientApplyFps = clientFrames * 1000 / elapsed;
    app.displayedDrawFps = drawFrames * 1000 / elapsed;
    app.fpsWindowStart = app.now;
    app.fpsWindowCpuFrame = app.cpuFrame;
    app.fpsWindowClientFrame = client.lastCompleteFrameId;
    app.fpsWindowDrawFrames = app.drawFrames;
  }

  function diagnoseBottleneck() {
    const target = Math.min(app.targetFps, 30);
    const drawFps = app.displayedDrawFps;
    const sourceFps = app.displayedMiaUpdateFps;
    const applyFps = app.displayedClientApplyFps;

    if (app.fpsWindowStart === 0 || drawFps === 0) {
      return "warming up";
    }

    if (drawFps + 1 < target && sourceFps + 1 >= drawFps && applyFps + 1 >= drawFps) {
      return "render loop limited";
    }

    if (applyFps + 1 < Math.min(target, sourceFps) && drawFps + 1 >= target) {
      if (app.drops > 0 || app.nacks > 0 || app.packetLoss > 0) {
        return "network repair limited";
      }
      if (mia.pendingResponse || client.pending) {
        return "protocol response pending";
      }
      return "client cadence limited";
    }

    if (sourceFps + 1 < 30 && app.writeMode === "ack") {
      return "6502 ack-paced";
    }

    return "none";
  }

  function appendPacket(kind, dropped, label) {
    app.packetHistory.push({ kind, dropped, label });
    if (app.packetHistory.length > 72) {
      app.packetHistory.splice(0, app.packetHistory.length - 72);
    }
  }

  function packetVisuals() {
    return app.packetHistory.map((packet) => {
      const span = document.createElement("span");
      const kind = packet.kind === "ack dropped" ? "ack dropped" : packet.kind;
      span.className = `packet ${kind}${packet.dropped ? " dropped" : ""}`;
      span.title = packet.label;
      return span;
    });
  }

  function summarizePackets() {
    const recent = app.packetHistory.slice(-8).map((packet) => packet.label);
    return recent.length === 0 ? "none" : recent.join(" ");
  }

  function setStatus(status) {
    app.lastStatus = status;
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

  function signExtend(value, bits) {
    const sign = 1 << (bits - 1);
    return (value & sign) ? value - (1 << bits) : value;
  }

  function positiveMod(value, modulo) {
    return ((value % modulo) + modulo) % modulo;
  }

  function hexToRgb565(hex) {
    const value = Number.parseInt(hex.slice(1), 16);
    return rgb565((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
  }

  function rgb565(r, g, b) {
    const red = Math.max(0, Math.min(31, Math.round(r * 31 / 255)));
    const green = Math.max(0, Math.min(63, Math.round(g * 63 / 255)));
    const blue = Math.max(0, Math.min(31, Math.round(b * 31 / 255)));
    return (red << 11) | (green << 5) | blue;
  }

  function formatBytes(bytes) {
    if (bytes < 1024) {
      return `${bytes} B`;
    }
    return `${(bytes / 1024).toFixed(1)} KiB`;
  }

  function glyphPixel(char, x, y) {
    const rows = fontRows(char);
    if (!rows || y >= 7 || x === 0 || x > 5) {
      return 0;
    }
    return ((rows[y] >> (5 - x)) & 1) ? 5 : 0;
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
