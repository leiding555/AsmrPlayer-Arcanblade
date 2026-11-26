"use strict";

const element = id => document.getElementById(id);
const views = ["pairing", "library", "export", "work", "offline", "player"];
const audio = element("audio");
const playbackMode = element("playback-mode");
const connection = element("connection");
const playbackStatus = element("playback-status");
const subtitles = element("subtitles");
const subtitleScroll = element("subtitle-scroll");
const subtitleCues = element("subtitle-cues");
const resumeSubtitles = element("resume-subtitles");
const subtitlePipButton = element("subtitle-pip");
const subtitlePipStatus = element("subtitle-pip-status");
const subtitlePipDiagnostics = element("subtitle-pip-diagnostics");
const subtitlePipReport = element("subtitle-pip-report");
const pipRequirementsMessage = "字幕画中画检测未通过。";
const pipRetryMessage = "字幕画面已准备，请再次点击「字幕画中画」打开小窗。";
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
const hosted = document.documentElement.dataset.offlineOnly === "true";
const inWechat = /MicroMessenger/i.test(navigator.userAgent);
const inEmbeddedBrowser = inWechat || /\bQQ\/\d/i.test(navigator.userAgent)
  || (/\bAndroid\b/i.test(navigator.userAgent) && /;\s*wv\s*\)/i.test(navigator.userAgent));
const standaloneMode = window.matchMedia("(display-mode: standalone)");
const isStandalone = () => standaloneMode.matches || navigator.standalone === true;
const isAppleMobile = () => /iPhone|iPad|iPod|Macintosh.*Mobile/i.test(navigator.userAgent);
const installLink = element("install-app");
const installHelp = element("install-help");
const installOffline = element("install-offline");
const home = hosted ? "offline" : "library";
let installPrompt = null;
let route;
let activeView;
let goingBack = false;
const scrollPositions = new Map();
let libraryWorks = [];
let exportWorks = new Map();
let work = null;
let workTracks = null;
let directoryTracks = [];
const byName = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });
let trackIndex = -1;
let cueRows = [];
let cueEvents = [];
let nextCueEvent = 0;
let activeCues = new Set();
let followingSubtitles = true;
let viewGeneration = 0;
let trackGeneration = 0;
let frame = 0;
let subtitleStatus = "";
let autoplayBlocked = false;
let subtitleRequest = null;
let subtitlePip = null;
let safariPipVideoResult = "";
let offlineReady = false;
let playingOffline = false;
let audioObjectUrl = "";
let coverObjectUrl = "";
let importing = null;
let receivingPackage = false;
let transferReceiver = null;
let packageTransfer = null;
let offlineCoverUrls = [];
let offlineRecords = null;
let selectedOfflineWorks = null;
let deletingOfflineWorks = false;
const packageForm = element("package-form");

function view(id) {
  activeView = id;
  for (const name of views) element(name).hidden = name !== id;
  document.body.classList.toggle("player-open", id === "player");
  element("tracks-button").hidden = id === "pairing" || !route?.depth;
  installLink.hidden = hosted || id === "pairing";
}

function saveScroll() {
  if (route && activeView !== "player" && activeView !== "pairing") {
    scrollPositions.set(route.key, window.scrollY);
  }
}

function setWorkSearchFields(card, name, folderName) {
  card.dataset.searchName = name.toLowerCase();
  card.dataset.searchFolder = (folderName ?? "").toLowerCase();
}

function filterWorks(page, list) {
  const input = element(`${page}-search`);
  const query = input.value.trim().toLowerCase();
  let hasMatch = false;
  for (const card of list.children) {
    const matches = !query || card.dataset.searchName.includes(query) || card.dataset.searchFolder.includes(query);
    card.hidden = !matches;
    hasMatch ||= matches;
  }
  element(`${page}-search-clear`).disabled = !input.value;
  element(`${page}-search-empty`).hidden = !query || !list.children.length || hasMatch;
  if (page === "offline") updateOfflineManagement();
}

function setRoute(next, depth, push) {
  const entry = { ...next, home, depth, key: Array.from(crypto.getRandomValues(new Uint32Array(2))).join("-") };
  if (push) history.pushState(entry, "");
  else history.replaceState(entry, "");
  route = entry;
  return entry;
}

function navigate(next) {
  if (goingBack) return;
  saveScroll();
  void renderRoute(setRoute(next, route.depth + 1, true), true);
}

async function renderRoute(entry, reuseListing = false) {
  const { page, item, workId } = entry;
  if (reuseListing && workTracks && activeView === page && work
      && ((page === "work" && work.id === item.id) || (page === "offline" && work.id === workId))) {
    renderDirectory(entry);
  } else if (page === "library") await showWorks(activeView === undefined);
  else if (page === "work" || page === "lan-player") {
    await showWork(entry);
    if (route !== entry || activeView !== "work") return;
  } else if (page === "export") {
    if (!libraryWorks.length) await showWorks();
    if (route !== entry || activeView === "pairing") return;
    showExport();
  } else {
    await showOffline(entry);
    if (route !== entry || activeView !== "offline") return;
  }
  if (page.endsWith("-player") && workTracks) {
    const index = directoryTracks.findIndex(track => track.id === entry.trackId);
    if (index < 0) return back();
    await (hosted ? playOffline : playTrack)(index);
  }
  if (route === entry) window.scrollTo({ top: scrollPositions.get(entry.key) ?? 0, behavior: "instant" });
}

function back(steps = 1) {
  if (route.depth < steps || goingBack) return;
  goingBack = true;
  history.go(-steps);
}

window.addEventListener("popstate", ({ state }) => {
  goingBack = false;
  if (state?.home !== home || !state.key) return;
  saveScroll();
  route = state;
  void renderRoute(route, true);
});

function changeTrack(index) {
  const track = directoryTracks[index];
  if (!track || !route.page.endsWith("-player")) return;
  route = { ...route, trackId: track.id };
  history.replaceState(route, "");
  (hosted ? playOffline : playTrack)(index);
}

function openTrack(track) {
  if (goingBack) return;
  saveScroll();
  setRoute({ ...route, page: hosted ? "offline-player" : "lan-player", trackId: track.id }, route.depth + 1, true);
  (hosted ? playOffline : playTrack)(directoryTracks.indexOf(track));
}

async function request(path, options) {
  const response = await fetch(path, options);
  const result = await response.json();
  if (!response.ok) {
    const error = new Error(result.error || "请求失败。");
    error.status = response.status;
    throw error;
  }
  return result;
}

function message(error) {
  return error instanceof TypeError || error instanceof SyntaxError
    ? "无法连接电脑，请确认手机播放仍开启且两台设备在同一网络。"
    : error.message;
}

function stopTrack() {
  ++trackGeneration;
  closeSubtitlePip();
  subtitleRequest?.abort();
  subtitleRequest = null;
  audio.pause();
  audio.removeAttribute("src");
  audio.load();
  if (audioObjectUrl) URL.revokeObjectURL(audioObjectUrl);
  if (coverObjectUrl) URL.revokeObjectURL(coverObjectUrl);
  audioObjectUrl = "";
  coverObjectUrl = "";
  playingOffline = false;
  cancelAnimationFrame(frame);
  frame = 0;
  cueRows = [];
  cueEvents = [];
  nextCueEvent = 0;
  activeCues.clear();
  followingSubtitles = true;
  subtitleCues.replaceChildren();
  subtitleScroll.scrollTo({ top: 0, behavior: "instant" });
  subtitles.hidden = true;
  resumeSubtitles.hidden = true;
  subtitlePipButton.disabled = true;
  subtitlePipStatus.textContent = "";
  subtitlePipDiagnostics.open = false;
  subtitlePipReport.textContent = "";
  safariPipVideoResult = "";
  subtitleStatus = "";
  autoplayBlocked = false;
  playbackStatus.textContent = "";
}

function showPairing(error) {
  ++viewGeneration;
  stopTrack();
  libraryWorks = [];
  exportWorks = new Map();
  view("pairing");
  connection.textContent = error ? message(error) : "";
}

async function showWorks(initial = false) {
  const generation = ++viewGeneration;
  stopTrack();
  if (!initial) view("library");
  connection.textContent = "正在读取作品库…";
  try {
    const result = await request("/api/works");
    if (generation !== viewGeneration) return;
    libraryWorks = result.works;
    exportWorks = new Map(libraryWorks.map(item => [item.id, exportWorks.get(item.id) ?? {
      item, tracks: null, selected: new Set(), expanded: false, loading: false, error: ""
    }]));
    const grid = element("works");
    grid.replaceChildren();
    for (const item of result.works) {
      const card = document.createElement("button");
      card.className = "work-card";
      setWorkSearchFields(card, item.name, item.folder_name);
      const image = document.createElement("img");
      image.src = `/api/works/${item.id}/cover`;
      image.alt = "";
      image.loading = "lazy";
      image.addEventListener("error", () => { image.src = "/mark.png"; }, { once: true });
      card.append(image, document.createTextNode(item.name));
      card.addEventListener("click", () => navigate({ page: "work", item, directory: "" }));
      grid.append(card);
    }
    filterWorks("library", grid);
    connection.textContent = result.works.length ? "" : "作品库为空。";
    element("batch-open").disabled = !libraryWorks.length;
    view("library");
  } catch (error) {
    if (generation !== viewGeneration) return;
    showPairing(error.status === 401 && initial ? null : error);
  }
}

function renderBreadcrumb(nav, directory) {
  const names = ["作品根", ...(directory ? directory.split("/") : [])];
  const current = names.length - 1;
  nav.replaceChildren();
  nav.hidden = false;
  nav.classList.toggle("deep-path", current >= 4);

  function ancestor(level) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = button.title = names[level];
    button.addEventListener("click", () => back(current - level));
    return button;
  }

  if (current > 0) nav.append(ancestor(0));
  if (current > 1) {
    const overflow = document.createElement("details");
    overflow.className = "breadcrumb-overflow";
    const toggle = document.createElement("summary");
    toggle.textContent = "…";
    toggle.setAttribute("aria-label", "选择上级目录");
    const menu = document.createElement("div");
    for (let level = 1; level < current; ++level) {
      const button = ancestor(level);
      button.className = "breadcrumb-middle";
      nav.append(button);
      menu.append(ancestor(level));
    }
    overflow.append(toggle, menu);
    nav.append(overflow);
  }
  const label = document.createElement("span");
  label.textContent = label.title = names[current];
  label.setAttribute("aria-current", "location");
  nav.append(label);
}

async function removeOfflineTrack(record, button) {
  if (!window.confirm(`从本机离线库删除「${record.trackName}」及其字幕？`)) return;
  const generation = viewGeneration;
  button.disabled = true;
  try {
    await offlineStore.remove(record);
    if (route.page === "offline") {
      saveScroll();
      await renderRoute(route);
    }
  } catch {
    if (generation === viewGeneration && activeView === "offline") connection.textContent = "无法删除已保存音轨，请重试。";
    button.disabled = false;
  }
}

function renderDirectory(entry) {
  // A restored player entry can predate directory navigation, or the track may
  // have gained directory metadata since this history entry was created.
  const playing = entry.trackId && workTracks.find(track => track.id === entry.trackId);
  const directory = playing ? trackDirectory(playing) : entry.directory || "";
  const prefix = directory ? directory + "/" : "";
  const folders = new Set();
  directoryTracks = [];
  for (const track of workTracks) {
    const parent = trackDirectory(track);
    if (parent === directory) directoryTracks.push(track);
    else if (parent.startsWith(prefix)) folders.add(parent.slice(prefix.length).split("/", 1)[0]);
  }
  const trackName = track => hosted ? track.trackName : track.name;
  directoryTracks.sort((a, b) => byName.compare(trackName(a), trackName(b)));
  renderBreadcrumb(element(hosted ? "offline-breadcrumb" : "work-breadcrumb"), directory);
  const rows = document.createDocumentFragment();
  for (const name of [...folders].sort(byName.compare)) {
    const row = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "directory-button";
    button.textContent = name;
    button.setAttribute("aria-label", `打开文件夹 ${name}`);
    button.dataset.directory = prefix + name;
    button.addEventListener("click", () => navigate({ ...entry, directory: prefix + name }));
    row.append(button);
    rows.append(row);
  }
  for (const track of directoryTracks) {
    const row = document.createElement("li");
    const play = document.createElement("button");
    play.type = "button";
    play.textContent = trackName(track);
    play.dataset.trackId = track.id;
    play.addEventListener("click", () => openTrack(track));
    row.append(play);
    if (hosted) {
      const detail = document.createElement("small");
      detail.textContent = fileSize(track.size);
      play.append(detail);
      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "删除";
      remove.setAttribute("aria-label", `删除 ${track.trackName}`);
      remove.addEventListener("click", () => removeOfflineTrack(track, remove));
      row.append(remove);
    }
    rows.append(row);
  }
  element(hosted ? "offline-tracks" : "tracks").replaceChildren(rows);
  connection.textContent = "";
  if (!folders.size && !directoryTracks.length) {
    connection.textContent = directory ? "此目录没有可播放的音轨。"
      : hosted ? "该作品没有已保存的音轨。" : "此作品没有可播放的 MP3、WAV 或 FLAC 音轨。";
  }
}

function trackDirectory(track) {
  return track.directory && track.directory !== "." ? track.directory : "";
}

async function showWork(entry) {
  const { item } = entry;
  const generation = ++viewGeneration;
  stopTrack();
  work = item;
  workTracks = null;
  directoryTracks = [];
  element("work-name").textContent = item.name;
  element("work-breadcrumb").hidden = true;
  element("tracks").replaceChildren();
  packageForm.hidden = true;
  element("package-open").hidden = true;
  element("package-help").hidden = true;
  element("hosted-library").hidden = true;
  connection.textContent = "正在读取音轨…";
  view("work");
  try {
    const result = await request(`/api/works/${item.id}/tracks`);
    if (generation !== viewGeneration) return;
    work = { id: item.id, name: result.name };
    workTracks = result.tracks;
    element("package-open").hidden = !workTracks.some(isExportableTrack);
    element("work-name").textContent = result.name;
    renderDirectory(entry);
  } catch (error) {
    if (generation !== viewGeneration) return;
    if (error.status === 401) showPairing(error);
    else connection.textContent = message(error);
  }
}

function updatePackageSelection() {
  const enabled = [...element("package-tracks").querySelectorAll("input")];
  const chosen = enabled.filter(input => input.checked);
  const total = chosen.reduce((sum, input) => sum + Number(input.dataset.size), 0);
  const all = element("package-all");
  all.checked = chosen.length === enabled.length;
  all.indeterminate = chosen.length > 0 && !all.checked;
  element("package-selection").value = chosen.map(input => input.value).join(",");
  element("package-size").textContent = `已选 ${chosen.length} 条 · 约 ${fileSize(total)}`;
  element("package-submit").disabled = chosen.length === 0 || chosen.length > 4096;
  if (chosen.length > 4096) element("package-size").textContent += " · 一次最多 4096 条";
}

function openPackageSelection() {
  if (!work) return;
  const list = element("package-tracks");
  list.replaceChildren();
  for (const track of workTracks) {
    if (!isExportableTrack(track)) continue;
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.value = track.id;
    input.dataset.size = track.size;
    input.checked = true;
    label.append(input, exportTrackName(track));
    list.append(label);
  }
  packageForm.action = `/api/works/${work.id}/offline-package`;
  packageForm.hidden = false;
  updatePackageSelection();
}

function isExportableTrack(track) {
  return Number.isSafeInteger(track.size) && track.size > 0;
}

function exportTrackName(track) {
  return track.directory && track.directory !== "." ? `${track.directory} / ${track.name}` : track.name;
}

function selectedExports() {
  return libraryWorks.flatMap(item => {
    const group = exportWorks.get(item.id);
    const chosen = group.tracks?.filter(track => group.selected.has(track.id)) ?? [];
    return chosen.length ? [{
      name: item.name,
      packageUrl: `/api/works/${item.id}/offline-package`,
      selection: chosen.map(track => track.id).join(","),
      count: chosen.length,
      bytes: chosen.reduce((sum, track) => sum + track.size, 0)
    }] : [];
  });
}

function updateExportSummary() {
  const packages = selectedExports();
  const count = packages.reduce((sum, pack) => sum + pack.count, 0);
  const bytes = packages.reduce((sum, pack) => sum + pack.bytes, 0);
  const overLimit = packages.some(pack => pack.count > 4096);
  element("export-size").textContent = `已选 ${packages.length} 部作品 · ${count} 条音轨 · 约 ${fileSize(bytes)}`
    + (overLimit ? " · 每部作品一次最多 4096 条" : "");
  element("package-export").disabled = !!packageTransfer || !packages.length || overLimit
    || [...exportWorks.values()].some(group => group.loading);
}

async function loadExportTracks(group, selectAll = false) {
  if (group.loading || group.tracks) return;
  group.loading = true;
  group.error = "";
  group.selectAll = selectAll;
  renderExportGroup(group);
  updateExportSummary();
  try {
    const result = await request(`/api/works/${group.item.id}/tracks`);
    group.tracks = result.tracks.filter(isExportableTrack);
    if (selectAll) group.selected = new Set(group.tracks.map(track => track.id));
  } catch (error) {
    group.error = message(error);
    group.expanded = true;
    if (error.status === 401 && activeView === "export") showPairing(error);
  } finally {
    group.loading = false;
    group.selectAll = false;
    if (activeView === "export" && exportWorks.get(group.item.id) === group) {
      renderExportGroup(group);
      updateExportSummary();
    }
  }
}

function renderExportGroup(group) {
  const { item, tracks, selected, expanded, loading, error, controls } = group;
  if (!controls) return;
  const { select, toggle, list } = controls;
  select.checked = !!group.selectAll || !!tracks?.length && selected.size === tracks.length;
  select.indeterminate = selected.size > 0 && !select.checked;
  select.disabled = loading || tracks?.length === 0;
  toggle.textContent = expanded ? "收起" : "展开";
  toggle.setAttribute("aria-expanded", expanded);
  list.hidden = !expanded;
  list.replaceChildren();
  if (!expanded) return;
  if (!tracks?.length) {
    const status = document.createElement("p");
    status.textContent = loading ? "正在读取音轨…" : error || "没有可导出的音轨。";
    list.append(status);
    if (error) {
      const retry = document.createElement("button");
      retry.type = "button";
      retry.textContent = "重试";
      retry.addEventListener("click", () => void loadExportTracks(group));
      list.append(retry);
    }
    return;
  }
  for (const track of tracks) {
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.className = "export-track-select";
    input.value = track.id;
    input.checked = selected.has(track.id);
    input.addEventListener("change", () => {
      if (input.checked) selected.add(track.id);
      else selected.delete(track.id);
      select.checked = selected.size === tracks.length;
      select.indeterminate = selected.size > 0 && !select.checked;
      updateExportSummary();
    });
    label.append(input, exportTrackName(track));
    list.append(label);
  }
}

function showExport() {
  ++viewGeneration;
  stopTrack();
  connection.textContent = "";
  view("export");
  const container = element("export-works");
  container.replaceChildren();
  for (const item of libraryWorks) {
    const group = exportWorks.get(item.id);
    const article = document.createElement("article");
    article.className = "export-work";
    article.dataset.workId = item.id;
    setWorkSearchFields(article, item.name, item.folder_name);
    const heading = document.createElement("div");
    heading.className = "export-work-heading";
    const label = document.createElement("label");
    const select = document.createElement("input");
    select.type = "checkbox";
    select.className = "export-work-select";
    label.append(select, item.name);
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "export-work-toggle";
    toggle.setAttribute("aria-label", `${item.name}音轨`);
    const list = document.createElement("div");
    list.className = "export-work-tracks";
    list.id = `export-work-${item.id}`;
    toggle.setAttribute("aria-controls", list.id);
    group.controls = { select, toggle, list };
    select.addEventListener("change", () => {
      if (!group.tracks) {
        void loadExportTracks(group, select.checked);
        return;
      }
      group.selected = new Set(select.checked ? group.tracks.map(track => track.id) : []);
      renderExportGroup(group);
      updateExportSummary();
    });
    toggle.addEventListener("click", () => {
      group.expanded = !group.expanded;
      renderExportGroup(group);
      if (group.expanded) void loadExportTracks(group);
    });
    heading.append(label, toggle);
    article.append(heading, list);
    container.append(article);
    renderExportGroup(group);
  }
  filterWorks("export", container);
  updateExportSummary();
}

async function startPackageExport() {
  if (element("package-export").disabled || activeView !== "export") return;
  const packages = selectedExports();
  const status = element("package-transfer-status");
  const cancel = element("cancel-package-transfer");
  const controller = new AbortController();
  packageTransfer = controller;
  element("package-transfer").hidden = false;
  cancel.hidden = false;
  const setStatus = text => { status.textContent = text; };
  updateExportSummary();
  try {
    const result = await exportOfflinePackages({
      receiverUrl: installLink.href,
      packages,
      signal: controller.signal,
      setStatus
    });
    setStatus(`已导入手机离线库 · ${result.completedCount} 部作品。`);
  } catch (error) {
    const progress = `已导入 ${error.completedCount ?? 0}/${packages.length} 部作品。`;
    const stopped = error.packageName ? `停在「${error.packageName}」。` : "";
    setStatus(controller.signal.aborted ? `已取消导出。${progress}${stopped}`
      : `${progress}${stopped}${message(error)} 可在作品详情下载压缩包后手动导入。`);
  } finally {
    packageTransfer = null;
    cancel.hidden = true;
    updateExportSummary();
  }
}

function cueTimestamp(ms) {
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const tail = String(seconds % 60).padStart(2, "0");
  return minutes < 60 ? `${minutes}:${tail}`
    : `${Math.floor(minutes / 60)}:${String(minutes % 60).padStart(2, "0")}:${tail}`;
}

function safariPipInterfaceStatus(videoApi) {
  const canCheck = typeof videoApi?.webkitSupportsPresentationMode === "function";
  const canSet = typeof videoApi?.webkitSetPresentationMode === "function";
  if (canCheck && canSet) return `已提供（${safariPipVideoResult || "当前字幕视频未检测"}）`;
  if (canCheck) return "缺少切换接口";
  if (canSet) return "缺少检测接口";
  return "未提供";
}

function checkSafariPipVideo(video) {
  if (typeof video.webkitSupportsPresentationMode !== "function"
      || typeof video.webkitSetPresentationMode !== "function" || video.readyState < 1) return "";
  try {
    return video.webkitSupportsPresentationMode("picture-in-picture")
      ? "当前字幕视频支持" : "当前字幕视频不支持";
  } catch {
    return "当前字幕视频检测失败";
  }
}

function updateSubtitlePipButton() {
  const enabled = document.pictureInPictureEnabled;
  const videoApi = window.HTMLVideoElement?.prototype;
  const canRequest = typeof videoApi?.requestPictureInPicture === "function";
  const canCapture = typeof window.HTMLCanvasElement?.prototype.captureStream === "function";
  const supported = enabled === true && canRequest && canCapture;
  const active = !!subtitlePip && document.pictureInPictureElement === subtitlePip.video;
  subtitlePipButton.setAttribute("aria-pressed", String(active));
  subtitlePipButton.disabled = !cueRows.length || !!audio.error || !supported || !!subtitlePip?.opening;
  if (!cueRows.length) return;

  const entry = isStandalone()
    ? (hosted ? "主屏幕离线库" : "局域网播放（主屏幕）")
    : (hosted ? "离线库（浏览器标签页）" : "局域网播放（浏览器标签页）");
  const switchState = enabled === true ? "已启用" : enabled === false ? "未启用" : "未提供";
  subtitlePipReport.textContent = [
    `打开方式：${entry}`,
    `网页画中画开关：${switchState}`,
    `标准画中画接口：${canRequest ? "已提供" : "未提供"}`,
    `画布视频流接口：${canCapture ? "已提供" : "未提供"}`,
    `Safari 专用接口：${safariPipInterfaceStatus(videoApi)}`,
  ].join("\n");
  if (!supported) {
    subtitlePipStatus.textContent = pipRequirementsMessage;
  } else if (subtitlePipStatus.textContent === pipRequirementsMessage) {
    subtitlePipStatus.textContent = "";
  }
}

function closeSubtitlePip() {
  const session = subtitlePip;
  if (!session) return;
  subtitlePip = null;
  if (document.pictureInPictureElement === session.video) {
    void document.exitPictureInPicture().catch(() => {});
  }
  session.video.pause();
  session.video.srcObject = null;
  for (const track of session.stream.getTracks()) track.stop();
  subtitlePipStatus.textContent = "";
  updateSubtitlePipButton();
}

function renderSubtitlePip() {
  const session = subtitlePip;
  if (!session) return;
  const cue = audio.ended ? -1 : latestActiveCue(activeCues);
  const text = cue < 0 ? "" : cueRows[cue].text;
  if (text === session.text) return;
  session.text = text;
  const { context } = session;
  const { width, height } = context.canvas;
  context.fillStyle = "#151a1d";
  context.fillRect(0, 0, width, height);
  if (!text) return;

  context.font = "44px sans-serif";
  const maxWidth = width - 64;
  const lines = [];
  for (const paragraph of text.split(/\r?\n/)) {
    let line = "";
    for (const character of paragraph) {
      if (line && context.measureText(line + character).width > maxWidth) {
        lines.push(line);
        line = "";
      }
      if (lines.length === 5) break;
      line += character;
    }
    if (lines.length === 5) {
      lines[4] = Array.from(lines[4]).slice(0, -1).join("") + "…";
      break;
    }
    lines.push(line);
  }
  context.fillStyle = "#f0f6f3";
  context.textAlign = "center";
  context.textBaseline = "middle";
  lines.forEach((line, index) => {
    context.fillText(line, width / 2, height / 2 + (index - (lines.length - 1) / 2) * 54, maxWidth);
  });
}

async function toggleSubtitlePip() {
  if (subtitlePipButton.disabled) return;
  if (subtitlePip && document.pictureInPictureElement === subtitlePip.video) {
    closeSubtitlePip();
    return;
  }

  let session = subtitlePip;
  const retrying = session !== null;
  let requestingWindow = false;
  let timeout;
  subtitlePipButton.disabled = true;
  subtitlePipStatus.textContent = "正在打开字幕画中画…";
  try {
    if (!session) {
      safariPipVideoResult = "";
      const canvas = document.createElement("canvas");
      canvas.width = 640;
      canvas.height = 360;
      const context = canvas.getContext("2d", { alpha: false });
      if (!context) throw new DOMException("无法创建字幕画布", "NotSupportedError");
      const video = document.createElement("video");
      video.muted = true;
      video.playsInline = true;
      const stream = canvas.captureStream();
      session = subtitlePip = { video, context, stream, opening: true };
      video.addEventListener("leavepictureinpicture", () => {
        if (subtitlePip === session) closeSubtitlePip();
      }, { once: true });
      video.srcObject = stream;
      renderSubtitlePip();
      await Promise.race([video.play(), new Promise((_, reject) => {
        timeout = setTimeout(() => reject(new DOMException("字幕画面启动超时", "TimeoutError")), 5000);
      })]);
      if (subtitlePip !== session) return;
      safariPipVideoResult = checkSafariPipVideo(session.video);
      if (navigator.userActivation?.isActive === false) {
        subtitlePipStatus.textContent = pipRetryMessage;
        return;
      }
    }
    // On a retry, request PiP directly in the click handler with the prepared video.
    session.opening = true;
    requestingWindow = true;
    await session.video.requestPictureInPicture();
    if (subtitlePip !== session) {
      if (document.pictureInPictureElement === session.video) await document.exitPictureInPicture();
      return;
    }
    subtitlePipStatus.textContent = "字幕画中画已开启，可以切换到其他应用。";
  } catch (error) {
    if (subtitlePip !== session) return;
    if (requestingWindow && !retrying && error.name === "NotAllowedError") {
      subtitlePipStatus.textContent = pipRetryMessage;
      return;
    }
    closeSubtitlePip();
    subtitlePipStatus.textContent = requestingWindow
      ? `无法打开字幕画中画（${error.name}）。`
      : `字幕画面启动失败（${error.name}）。`;
  } finally {
    clearTimeout(timeout);
    if (session && subtitlePip === session) session.opening = false;
    updateSubtitlePipButton();
  }
}

function installSubtitles(entries) {
  const rows = document.createDocumentFragment();
  cueRows = [...entries].sort(([a], [b]) => a - b).map(([start, end, text], index) => {
    if (end > start) {
      cueEvents.push({ time: start / 1000, index, starts: true }, { time: end / 1000, index, starts: false });
    }
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "subtitle-cue";
    button.dataset.cue = index;
    const timeLabel = document.createElement("span");
    timeLabel.className = "cue-time";
    timeLabel.textContent = cueTimestamp(start);
    const cueText = document.createElement("span");
    cueText.className = "cue-text";
    cueText.textContent = text;
    button.append(timeLabel, cueText);
    item.append(button);
    rows.append(item);
    return { start, text, button };
  });
  cueEvents.sort((a, b) => a.time - b.time);
  subtitleCues.replaceChildren(rows);
  subtitles.hidden = !cueRows.length;
  updateSubtitlePipButton();
  renderSubtitles();
}

function latestActiveCue(cues) {
  let latest = -1;
  for (const index of cues) latest = Math.max(latest, index);
  return latest;
}

function followCue(index) {
  if (index < 0 || subtitles.hidden) return;
  const row = cueRows[index].button.getBoundingClientRect();
  const viewport = subtitleScroll.getBoundingClientRect();
  const top = viewport.top + subtitleScroll.clientTop;
  const height = subtitleScroll.clientHeight;
  const upper = top + height * .2;
  const lower = top + height * .8;
  const rowFits = row.height <= lower - upper;
  if (row.top >= upper && (rowFits ? row.bottom <= lower : row.top < lower)) return;

  const target = Math.max(0, Math.min(subtitleScroll.scrollTop + row.top - top - height * .3,
    subtitleScroll.scrollHeight - height));
  if (Math.abs(target - subtitleScroll.scrollTop) < 1) return;
  subtitleScroll.scrollTo({ top: target, behavior: reducedMotion.matches ? "instant" : "smooth" });
}

function renderSubtitles() {
  const time = audio.currentTime;
  if (!Number.isFinite(time)) return;
  // Only update highlights when playback crosses a cue boundary, in either direction.
  if (!(nextCueEvent < cueEvents.length && cueEvents[nextCueEvent].time <= time)
      && !(nextCueEvent > 0 && cueEvents[nextCueEvent - 1].time > time)) return;

  const previousCue = latestActiveCue(activeCues);
  const nextActive = new Set(activeCues);
  while (nextCueEvent < cueEvents.length && cueEvents[nextCueEvent].time <= time) {
    const { index, starts } = cueEvents[nextCueEvent++];
    if (starts) nextActive.add(index);
    else nextActive.delete(index);
  }
  while (nextCueEvent > 0 && cueEvents[nextCueEvent - 1].time > time) {
    const { index, starts } = cueEvents[--nextCueEvent];
    if (starts) nextActive.delete(index);
    else nextActive.add(index);
  }
  for (const index of activeCues) {
    if (!nextActive.has(index)) cueRows[index].button.classList.remove("is-active");
  }
  for (const index of nextActive) {
    if (!activeCues.has(index)) cueRows[index].button.classList.add("is-active");
  }
  activeCues = nextActive;
  const currentCue = latestActiveCue(activeCues);
  if (currentCue === previousCue) return;
  if (previousCue >= 0) cueRows[previousCue].button.removeAttribute("aria-current");
  if (currentCue >= 0) cueRows[currentCue].button.setAttribute("aria-current", "true");
  if (followingSubtitles) followCue(currentCue);
}

function syncSubtitles() {
  renderSubtitles();
  renderSubtitlePip();
}

function pauseSubtitleFollowing() {
  if (!followingSubtitles || subtitles.hidden) return;
  followingSubtitles = false;
  subtitleScroll.scrollTo({ top: subtitleScroll.scrollTop, behavior: "instant" });
  resumeSubtitles.hidden = false;
}

function tick() {
  syncSubtitles();
  frame = audio.paused || audio.ended ? 0 : requestAnimationFrame(tick);
}

function startAudio(generation) {
  audio.play().catch(error => {
    if (generation !== trackGeneration || error.name !== "NotAllowedError") return;
    autoplayBlocked = true;
    playbackStatus.textContent = "请点击播放器的播放按钮。";
  });
}

function setSubtitleStatus(message) {
  subtitleStatus = message;
  if (!audio.error && !autoplayBlocked) playbackStatus.textContent = message;
}

function setTrackIndex(index) {
  trackIndex = index;
  element("previous").disabled = index === 0;
  element("next").disabled = index === directoryTracks.length - 1;
}

function playTrack(index) {
  if (!work || index < 0 || index >= directoryTracks.length) return;
  stopTrack();
  const generation = trackGeneration;
  const track = directoryTracks[index];
  setTrackIndex(index);
  connection.textContent = "";
  element("playing-work").textContent = work.name;
  element("playing-track").textContent = track.name;
  element("cover").src = `/api/works/${work.id}/cover`;
  view("player");

  const path = `/api/works/${work.id}/tracks/${track.id}`;
  audio.src = path + "/audio";
  startAudio(generation);
  subtitleRequest = new AbortController();
  request(path + "/subtitles", { signal: subtitleRequest.signal }).then(result => {
    if (generation !== trackGeneration) return;
    installSubtitles(result.entries);
    setSubtitleStatus(subtitleMessage(result.state));
  }).catch(error => {
    if (generation !== trackGeneration || error.name === "AbortError") return;
    if (error.status === 401) showPairing(error);
    else setSubtitleStatus("暂时无法读取字幕。");
  });
}

function subtitleMessage(state) {
  return ({
    none: "此音轨没有可用的绑定字幕。",
    disabled: "此音轨的字幕已关闭。",
    unavailable: "无法加载此音轨的字幕。"
  })[state] || "";
}

function fileSize(bytes) {
  return bytes >= 1024 ** 3 ? `${(bytes / 1024 ** 3).toFixed(2)} GB`
    : `${(bytes / 1024 ** 2).toFixed(1)} MB`;
}

function updateImportControls() {
  const busy = !!importing || receivingPackage;
  element("import-packs").hidden = !offlineReady || busy || selectedOfflineWorks !== null;
  element("cancel-import").hidden = !busy;
}

function updateOfflineManagement() {
  const atHome = offlineReady && route?.page === "offline" && !route.workId;
  const managing = selectedOfflineWorks !== null;
  const manage = element("offline-manage");
  manage.hidden = !atHome || !offlineRecords;
  manage.disabled = deletingOfflineWorks || (!managing && !offlineRecords?.length);
  manage.textContent = managing ? "完成" : "管理";
  manage.setAttribute("aria-expanded", String(managing));
  element("offline-management-bar").hidden = !atHome || !managing;
  const cards = element("offline-works").children;
  let visibleCount = 0;
  let checkedCount = 0;
  for (const card of cards) {
    const checked = selectedOfflineWorks?.has(card.dataset.workId) === true;
    if (managing) card.setAttribute("aria-pressed", String(checked));
    else card.removeAttribute("aria-pressed");
    card.disabled = managing && deletingOfflineWorks;
    if (!card.hidden) {
      ++visibleCount;
      if (checked) ++checkedCount;
    }
  }
  const all = element("offline-select-all");
  all.disabled = deletingOfflineWorks || !visibleCount;
  const allChecked = visibleCount > 0 && checkedCount === visibleCount;
  all.setAttribute("aria-pressed", String(allChecked));
  all.textContent = (allChecked ? "取消全选" : "全选") + (element("offline-search").value.trim() ? "搜索结果" : "");
  const remove = element("offline-delete-selected");
  remove.disabled = deletingOfflineWorks || !offlineRecords || !selectedOfflineWorks?.size;
  remove.textContent = deletingOfflineWorks ? "正在删除…" : `删除所选（${selectedOfflineWorks?.size ?? 0} 部）`;
  updateImportControls();
}

function setManagementStatus(message) {
  const status = element("offline-management-status");
  status.textContent = message;
  status.hidden = !message;
}

function renderOfflineHome(entries) {
  offlineRecords = entries.map(({ id, workId, file }) => ({ id, workId, file }));
  const works = new Map();
  for (const record of entries) {
    if (!works.has(record.workId)) works.set(record.workId, []);
    works.get(record.workId).push(record);
  }
  for (const id of selectedOfflineWorks ?? []) {
    if (!works.has(id)) selectedOfflineWorks.delete(id);
  }
  for (const url of offlineCoverUrls) URL.revokeObjectURL(url);
  offlineCoverUrls = [];
  const cards = document.createDocumentFragment();
  for (const [id, records] of works) {
    const card = document.createElement("button");
    card.className = "work-card";
    card.type = "button";
    card.dataset.workId = id;
    const folderName = records.find(record => record.workFolderName)?.workFolderName;
    setWorkSearchFields(card, records[0].workName, folderName);
    const image = document.createElement("img");
    const url = records[0].cover ? URL.createObjectURL(records[0].cover) : "mark.png";
    if (records[0].cover) offlineCoverUrls.push(url);
    image.src = url;
    image.alt = "";
    card.append(image, `${records[0].workName} · ${records.length} 条`);
    card.addEventListener("click", () => {
      if (!selectedOfflineWorks) return navigate({ page: "offline", workId: id, directory: "" });
      if (selectedOfflineWorks.has(id)) selectedOfflineWorks.delete(id);
      else selectedOfflineWorks.add(id);
      updateOfflineManagement();
    });
    cards.append(card);
  }
  const grid = element("offline-works");
  grid.replaceChildren(cards);
  filterWorks("offline", grid);
}

function updateOfflineUsage(entries) {
  const used = entries.reduce((sum, record) => sum + record.size + (record.cover?.size ?? 0), 0);
  element("offline-usage").textContent = entries.length
    ? `已保存 ${entries.length} 条音轨 · ${fileSize(used)}` : "暂无已保存音轨。";
}

async function deleteSelectedOfflineWorks() {
  const selected = selectedOfflineWorks;
  if (deletingOfflineWorks || !offlineRecords || !selected?.size) return;
  const workCount = selected.size;
  const records = offlineRecords.filter(record => selected.has(record.workId));
  if (!window.confirm(`从本机离线库删除选中的 ${workCount} 部作品，共 ${records.length} 条音轨？\n包含各子目录中的已保存音频和字幕。电脑原文件和已下载的 ZIP 会保留。`)) return;
  deletingOfflineWorks = true;
  updateOfflineManagement();
  setManagementStatus("");
  try {
    const entries = await offlineStore.removeWorks(records);
    if (route.page === "offline" && !route.workId) {
      renderOfflineHome(entries);
      if (selectedOfflineWorks === selected) {
        if (selected.size) {
          const remaining = entries.filter(record => selected.has(record.workId)).length;
          setManagementStatus(`还有 ${selected.size} 部作品、${remaining} 条音轨保存在本机，已保留勾选，可重试。`);
        } else {
          selectedOfflineWorks = null;
          setManagementStatus(`已删除 ${workCount} 部作品。`);
        }
      }
      updateOfflineUsage(entries);
    } else if (selected.has(route.workId)) {
      // History navigation may have reopened this work while deletion held the storage lock.
      const remaining = entries.filter(record => record.workId === route.workId);
      if (!remaining.length) back(route.depth);
      else if (activeView === "player") {
        workTracks = remaining;
        renderDirectory(route);
        const index = directoryTracks.findIndex(track => track.id === route.trackId);
        if (index < 0) back();
        else setTrackIndex(index);
      } else {
        saveScroll();
        await renderRoute(route);
      }
    }
  } catch {
    if (selectedOfflineWorks === selected) setManagementStatus("未能确认删除结果，已保留勾选，请重试。");
  } finally {
    deletingOfflineWorks = false;
    updateOfflineManagement();
  }
}

function showInstallGuide() {
  if (location.hash !== "#install") return;
  history.replaceState(history.state, "", location.pathname + location.search);
  if (isStandalone()) return;
  element("install-steps").textContent = (isAppleMobile()
    ? "在 Safari 点「分享 → 添加到主屏幕」；如果有「作为网页 App 打开」选项，请开启后点「添加」。"
    : "在浏览器菜单选择「添加到主屏幕」或「安装应用」。")
    + "添加后从桌面图标打开离线库，再导入下载的离线包。";
  installHelp.hidden = false;
}

function hideInstallGuide() {
  installPrompt = null;
  installOffline.hidden = true;
  installHelp.hidden = true;
}

async function showOffline(entry) {
  const { workId } = entry;
  const generation = ++viewGeneration;
  stopTrack();
  work = null;
  workTracks = null;
  directoryTracks = [];
  connection.textContent = "";
  view("offline");
  element("offline-heading").textContent = "离线库";
  element("offline-search-box").hidden = !!workId || !offlineReady;
  element("offline-breadcrumb").hidden = true;
  element("offline-tracks").replaceChildren();
  const grid = element("offline-works");
  offlineRecords = null;
  if (workId) {
    selectedOfflineWorks = null;
    setManagementStatus("");
  }
  grid.replaceChildren();
  filterWorks("offline", grid);
  for (const url of offlineCoverUrls) URL.revokeObjectURL(url);
  offlineCoverUrls = [];
  updateImportControls();
  if (!offlineReady) return;
  try {
    const entries = await offlineStore.list();
    if (generation !== viewGeneration) return;
    if (workId) {
      workTracks = entries.filter(record => record.workId === workId);
      if (!workTracks.length) {
        back(entry.depth);
        return;
      }
      work = { id: workId, name: workTracks[0].workName };
      element("offline-heading").textContent = work.name;
      renderDirectory(entry);
    } else {
      renderOfflineHome(entries);
    }
    updateOfflineUsage(entries);
  } catch {
    if (generation === viewGeneration) connection.textContent = "无法读取离线库。";
  }
}

async function importPackages(files, transferSignal) {
  if (!files.length || importing || (receivingPackage && !transferSignal)) return false;
  const status = element("import-status");
  importing = new AbortController();
  const { signal } = importing;
  const abort = () => importing?.abort();
  transferSignal?.addEventListener("abort", abort, { once: true });
  if (transferSignal?.aborted) abort();
  updateImportControls();
  navigator.storage.persist?.().catch(() => {});
  const counts = { added: 0, updated: 0, skipped: 0 };
  const errors = [];
  const summary = () => `新增 ${counts.added} 条，更新 ${counts.updated} 条，跳过 ${counts.skipped} 条。`;
  try {
    for (const [index, file] of files.entries()) {
      signal.throwIfAborted();
      status.textContent = `正在检查第 ${index + 1}/${files.length} 个离线包…`;
      try {
        const pack = await offlineZip.inspect(file);
        signal.throwIfAborted();
        let processed = 0;
        status.textContent = `正在导入「${pack.workName}」 0/${pack.tracks.length} 条…`;
        await offlineStore.importPackage(pack, signal, outcome => {
          counts[outcome]++;
          status.textContent = `正在导入「${pack.workName}」 ${++processed}/${pack.tracks.length} 条…`;
        });
      } catch (error) {
        if (signal.aborted || error.name === "QuotaExceededError") throw error;
        errors.push(`${file.name}：${error instanceof SyntaxError ? "包内数据无效。" : error.message}`);
      }
    }
    status.textContent = errors.length ? `${summary()} ${errors.length} 个包未完成。${errors[0]}`
      : `导入完成。${summary()}${transferSignal ? "" : " 可以删除「下载」里的原包以释放空间。"}`;
    return errors.length === 0;
  } catch (error) {
    status.textContent = `${signal.aborted ? "已取消导入。" : "手机存储空间不足。"}${summary()} 已完成的音轨仍可播放。`;
    return false;
  } finally {
    transferSignal?.removeEventListener("abort", abort);
    importing = null;
    updateImportControls();
    if (activeView === "offline") {
      saveScroll();
      await renderRoute(route);
    }
  }
}

async function playOffline(index) {
  const record = directoryTracks[index];
  if (!record) return;
  stopTrack();
  const generation = trackGeneration;
  playingOffline = true;
  setTrackIndex(index);
  connection.textContent = "";
  element("playing-work").textContent = record.workName;
  element("playing-track").textContent = record.trackName;
  if (record.cover) coverObjectUrl = URL.createObjectURL(record.cover);
  element("cover").src = coverObjectUrl || "mark.png";
  view("player");
  try {
    const file = await offlineStore.getFile(record);
    if (generation !== trackGeneration) return;
    // File.slice preserves the disk-backed data while supplying its original MIME type.
    audioObjectUrl = URL.createObjectURL(file.slice(0, file.size, record.mime));
    audio.src = audioObjectUrl;
    installSubtitles(record.subtitles.entries);
    setSubtitleStatus(subtitleMessage(record.subtitles.state));
    startAudio(generation);
  } catch {
    if (generation === trackGeneration) playbackStatus.textContent = "本地音频已缺失，请重新导入离线包。";
  }
}

async function pairWithCode(code) {
  const button = element("pair-form").querySelector("button");
  if (button.disabled) return;
  button.disabled = true;
  connection.textContent = "正在配对…";
  try {
    await request("/api/pair", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code })
    });
    element("code").value = "";
    await renderRoute(route);
  } catch (error) {
    showPairing(error);
  } finally {
    button.disabled = false;
  }
}

element("pair-form").addEventListener("submit", event => {
  event.preventDefault();
  void pairWithCode(element("code").value);
});

element("refresh").addEventListener("click", () => {
  saveScroll();
  exportWorks = new Map();
  void renderRoute(route);
});
for (const [page, listId] of [["library", "works"], ["export", "export-works"], ["offline", "offline-works"]]) {
  const input = element(`${page}-search`);
  const list = element(listId);
  input.addEventListener("input", () => filterWorks(page, list));
  element(`${page}-search-clear`).addEventListener("click", () => {
    input.value = "";
    input.focus({ preventScroll: true });
    filterWorks(page, list);
  });
}
element("tracks-button").addEventListener("click", () => back());
element("offline-manage").addEventListener("click", () => {
  selectedOfflineWorks = selectedOfflineWorks ? null : new Set();
  setManagementStatus("");
  updateOfflineManagement();
});
element("offline-select-all").addEventListener("click", event => {
  const deselect = event.currentTarget.getAttribute("aria-pressed") === "true";
  for (const card of element("offline-works").children) {
    if (card.hidden) continue;
    if (deselect) selectedOfflineWorks.delete(card.dataset.workId);
    else selectedOfflineWorks.add(card.dataset.workId);
  }
  updateOfflineManagement();
});
element("offline-delete-selected").addEventListener("click", deleteSelectedOfflineWorks);
element("batch-open").addEventListener("click", () => navigate({ page: "export" }));
element("package-open").addEventListener("click", openPackageSelection);
element("package-close").addEventListener("click", () => { packageForm.hidden = true; });
element("package-tracks").addEventListener("change", updatePackageSelection);
element("package-all").addEventListener("change", event => {
  for (const input of element("package-tracks").querySelectorAll("input")) input.checked = event.target.checked;
  updatePackageSelection();
});
element("package-export").addEventListener("click", startPackageExport);
element("cancel-package-transfer").addEventListener("click", () => packageTransfer?.abort());
packageForm.addEventListener("submit", event => {
  if (element("package-submit").disabled) {
    event.preventDefault();
    return;
  }
  element("package-help").hidden = false;
  element("hosted-library").hidden = false;
});
element("cancel-import").addEventListener("click", () => {
  if (importing) importing.abort();
  else transferReceiver?.cancel();
});
window.addEventListener("beforeinstallprompt", event => {
  if (installHelp.hidden || isStandalone()) return;
  event.preventDefault();
  installPrompt = event;
  installOffline.hidden = false;
});
window.addEventListener("appinstalled", hideInstallGuide);
installOffline.addEventListener("click", async () => {
  if (!installPrompt) return;
  const prompt = installPrompt;
  installPrompt = null;
  installOffline.hidden = true;
  try {
    await prompt.prompt();
    const { outcome } = await prompt.userChoice;
    if (outcome === "accepted") hideInstallGuide();
  } catch {
    // The manual steps stay visible if the browser cannot open its prompt.
  }
});
element("pack-files").addEventListener("change", event => {
  const files = Array.from(event.target.files);
  event.target.value = "";
  void importPackages(files);
});
element("previous").addEventListener("click", () => changeTrack(trackIndex - 1));
element("next").addEventListener("click", () => changeTrack(trackIndex + 1));
playbackMode.addEventListener("click", () => {
  audio.loop = !audio.loop;
  playbackMode.textContent = audio.loop ? "单曲循环" : "顺序播放";
  playbackMode.setAttribute("aria-label", audio.loop ? "单曲循环，切换为顺序播放" : "顺序播放，切换为单曲循环");
});
subtitlePipButton.addEventListener("click", toggleSubtitlePip);
standaloneMode.addEventListener?.("change", updateSubtitlePipButton);
document.addEventListener("visibilitychange", () => {
  if (subtitlePip) syncSubtitles();
});
window.addEventListener("pagehide", closeSubtitlePip);
subtitleScroll.addEventListener("wheel", pauseSubtitleFollowing, { passive: true });
subtitleScroll.addEventListener("touchmove", pauseSubtitleFollowing, { passive: true });
subtitleScroll.addEventListener("pointerdown", event => {
  if (event.pointerType === "mouse" && event.target === subtitleScroll) pauseSubtitleFollowing();
});
subtitleScroll.addEventListener("keydown", event => {
  if (["ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End"].includes(event.key)
      || (event.key === " " && event.target === subtitleScroll)) pauseSubtitleFollowing();
});
resumeSubtitles.addEventListener("click", () => {
  followingSubtitles = true;
  subtitleScroll.focus({ preventScroll: true });
  resumeSubtitles.hidden = true;
  const current = latestActiveCue(activeCues);
  if (current >= 0) {
    followCue(current);
    return;
  }
  // During a gap, return to the closest cue that has already started.
  const upcoming = cueRows.findIndex(cue => cue.start > audio.currentTime * 1000);
  followCue(Math.max(0, (upcoming < 0 ? cueRows.length : upcoming) - 1));
});
subtitleCues.addEventListener("click", event => {
  const button = event.target.closest(".subtitle-cue");
  if (!button || !audio.getAttribute("src") || audio.error) return;
  const index = Number(button.dataset.cue);
  const seconds = cueRows[index].start / 1000;
  if (Number.isFinite(audio.duration) && seconds > audio.duration) return;
  // Cancel a previous follow animation before a click chooses a new target.
  subtitleScroll.scrollTo({ top: subtitleScroll.scrollTop, behavior: "instant" });
  audio.currentTime = seconds;
  followingSubtitles = false;
  syncSubtitles();
  followingSubtitles = true;
  resumeSubtitles.hidden = true;
  followCue(index);
});
element("cover").addEventListener("error", event => {
  if (!event.target.src.endsWith("/mark.png")) event.target.src = "mark.png";
});
audio.addEventListener("playing", () => {
  autoplayBlocked = false;
  playbackStatus.textContent = subtitleStatus;
  if (!frame) tick();
  else syncSubtitles();
});
audio.addEventListener("waiting", () => {
  if (audio.getAttribute("src") && !autoplayBlocked) playbackStatus.textContent = "正在缓冲…";
});
for (const event of ["timeupdate", "seeking", "seeked", "pause"]) {
  audio.addEventListener(event, syncSubtitles);
}
audio.addEventListener("ended", () => {
  syncSubtitles();
  if (audio.ended && !audio.loop && activeView === "player" && !goingBack) changeTrack(trackIndex + 1);
});
audio.addEventListener("error", async () => {
  if (!audio.getAttribute("src")) return;
  closeSubtitlePip();
  updateSubtitlePipButton();
  if (playingOffline) {
    playbackStatus.textContent = "无法播放已保存的音轨。请检查文件是否完好，以及当前浏览器是否支持该音频格式。";
    return;
  }
  const generation = trackGeneration;
  playbackStatus.textContent = "无法播放。请检查电脑连接、音频文件，以及当前浏览器是否支持该音频格式。";
  try {
    const response = await fetch(audio.src, { method: "HEAD" });
    if (generation !== trackGeneration) return;
    if (response.status === 401) showPairing(new Error("配对已失效，请重新配对。"));
    else if (response.status === 404) playbackStatus.textContent = "音轨不可用，请返回作品库刷新。";
  } catch {
    if (generation === trackGeneration) {
      playbackStatus.textContent = "已断开电脑连接。恢复网络后可重新选择音轨播放。";
    }
  }
});
async function startPlayer() {
  const requiresBrowser = hosted ? inWechat : inEmbeddedBrowser && !isStandalone();
  if (requiresBrowser) {
    connection.textContent = inWechat
      ? "请从微信右上角菜单选择「在浏览器中打开」。"
      : "请从当前应用的菜单选择「在浏览器中打开」，或复制完整链接到浏览器打开。";
    return;
  }
  history.scrollRestoration = "manual";
  const saved = history.state;
  route = saved?.home === home && saved.key && !location.hash ? saved : setRoute({ page: home }, 0, false);
  if (hosted) {
    showInstallGuide();
    if (offlineStore.available) {
      try {
        await offlineStore.prepare();
        offlineReady = true;
      } catch {
        // The offline view shows the same guidance for unsupported or failed storage.
      }
    }
    await renderRoute(route);
    if (!offlineReady) connection.textContent = "无法启用离线存储，请使用 Safari 或 Chrome 打开。";
    transferReceiver = startOfflineTransferReceiver({
      available: offlineReady,
      setStatus: message => { element("import-status").textContent = message; },
      setReceiving: active => {
        receivingPackage = active;
        updateImportControls();
      },
      importPackage: async (file, signal) => ({ ok: await importPackages([file], signal) })
    });
    return;
  }
  const pairingFragment = location.hash;
  if (!pairingFragment.startsWith("#pair=")) {
    await renderRoute(route);
    return;
  }
  // The fragment stays in the browser. Clear it before sending the one-time code.
  history.replaceState(history.state, "", location.pathname + location.search);
  const code = /^#pair=([0-9]{6})$/.exec(pairingFragment)?.[1];
  if (code) void pairWithCode(code);
  else showPairing(new Error("扫码链接无效，请重新扫码或输入六位配对码。"));
}
void startPlayer();
