(() => {
  'use strict';

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => [...document.querySelectorAll(selector)];
  const statusPill = $('#statusPill');
  const statusMessage = $('#statusMessage');
  const scanButton = $('#scanButton');
  const deviceList = $('#deviceList');
  const savedList = $('#savedList');
  const activeCard = $('#activeCard');
  const state = { status: null, devices: [], saved: [], updates: true };
  let toastTimer;

  const labels = {
    starting: ['Starting', 'Preparing the Bluetooth controller…'],
    ready: ['Ready', 'Scan for nearby Bluetooth headphones and speakers.'],
    scanning: ['Scanning', 'Looking for nearby audio devices…'],
    connecting: ['Connecting', 'Pairing and establishing a secure audio link…'],
    connected: ['Connected', 'Negotiating the A2DP audio stream…'],
    streaming: ['Streaming', 'PS5 audio is playing on the connected device.'],
    disconnecting: ['Disconnecting', 'Closing the audio stream safely…'],
    error: ['Needs attention', 'The backend reported a problem. Try again.'],
    stopped: ['Stopped', 'The native backend has stopped.']
  };

  async function api(path, options = {}) {
    const response = await fetch(path, {
      cache: 'no-store',
      headers: { 'Content-Type': 'application/json' },
      ...options
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.message || `Request failed (${response.status})`);
    return data;
  }

  function toast(message) {
    clearTimeout(toastTimer);
    $('#toast').textContent = message;
    $('#toast').classList.add('is-visible');
    toastTimer = setTimeout(() => $('#toast').classList.remove('is-visible'), 2600);
  }

  function deviceCard(device, savedView = false) {
    const card = document.createElement('article');
    card.className = 'device-card';
    const busy = ['connecting', 'connected', 'streaming', 'disconnecting'].includes(state.status?.status);
    const active = state.status?.active?.mac === device.mac;
    const rssi = device.rssi <= -127 ? 'Signal —' : `${device.rssi} dBm`;
    card.innerHTML = `
      <div class="device-symbol" aria-hidden="true">◉</div>
      <div class="device-info">
        <h3></h3>
        <p class="device-meta"><span class="mac"></span><span>${rssi}</span>${device.saved ? '<span class="saved-badge">Saved</span>' : ''}</p>
      </div>
      <button class="device-action ${active ? 'disconnect' : ''}" ${busy && !active ? 'disabled' : ''}>${active ? 'Disconnect' : 'Connect'}</button>`;
    card.querySelector('h3').textContent = device.name || 'Unknown device';
    card.querySelector('.mac').textContent = device.mac;
    card.querySelector('button').addEventListener('click', () => active ? disconnect() : connect(device.mac));
    if (savedView) card.dataset.saved = 'true';
    return card;
  }

  function renderStatus(status) {
    state.status = status;
    const [label, message] = labels[status.status] || ['Unknown', 'Waiting for backend status…'];
    statusPill.className = `status-pill is-${status.status}`;
    statusPill.querySelector('span').textContent = label;
    statusMessage.textContent = status.error || message;
    scanButton.disabled = !['ready', 'error'].includes(status.status);
    scanButton.classList.toggle('is-loading', status.status === 'scanning');
    scanButton.querySelector('span:last-child').textContent = status.status === 'scanning' ? 'Scanning…' : 'Scan';
    $('#controllerState').textContent = status.controller ? 'Online' : 'Offline';
    $('#deviceCount').textContent = `${status.deviceCount} found`;

    if (status.active) {
      activeCard.classList.remove('is-hidden');
      activeCard.innerHTML = `<div><p class="eyebrow">Current device</p><h3></h3><p>${status.active.mac} · ${label}</p></div><button class="device-action disconnect">Disconnect</button>`;
      activeCard.querySelector('h3').textContent = status.active.name;
      activeCard.querySelector('button').addEventListener('click', disconnect);
    } else {
      activeCard.classList.add('is-hidden');
    }
  }

  function renderDevices(devices) {
    state.devices = devices;
    deviceList.replaceChildren(...devices.map((device) => deviceCard(device)));
    $('#deviceEmpty').classList.toggle('is-hidden', devices.length > 0);
  }

  function renderSaved(devices) {
    state.saved = devices;
    savedList.replaceChildren(...devices.map((device) => deviceCard(device, true)));
    $('#savedEmpty').classList.toggle('is-hidden', devices.length > 0);
  }

  async function refresh() {
    try {
      const [status, devices, saved] = await Promise.all([
        api('/api/status'), api('/api/devices'), api('/api/saved')
      ]);
      renderStatus(status);
      renderDevices(devices.devices);
      renderSaved(saved.devices);
    } catch (error) {
      statusPill.className = 'status-pill is-error';
      statusPill.querySelector('span').textContent = 'Offline';
      statusMessage.textContent = 'Cannot reach the native backend.';
      $('#controllerState').textContent = 'Offline';
    }
  }

  async function command(path, body) {
    try {
      const result = await api(path, { method: 'POST', body: body ? JSON.stringify(body) : '{}' });
      toast(result.message);
      await refresh();
    } catch (error) {
      toast(error.message);
    }
  }

  const connect = (mac) => command('/api/connect', { mac });
  const disconnect = () => command('/api/disconnect');
  scanButton.addEventListener('click', () => command('/api/scan'));

  $$('.tab').forEach((tab) => tab.addEventListener('click', () => {
    $$('.tab').forEach((item) => {
      const selected = item === tab;
      item.classList.toggle('is-active', selected);
      item.setAttribute('aria-selected', String(selected));
    });
    $$('.view').forEach((view) => view.classList.toggle('is-active', view.id === `${tab.dataset.view}View`));
    const first = $(`#${tab.dataset.view}View button`);
    if (first) first.focus();
  }));

  function setupToggle(button, key, initial, apply) {
    let enabled = localStorage.getItem(key);
    enabled = enabled === null ? initial : enabled === 'true';
    const update = () => {
      button.classList.toggle('is-on', enabled);
      button.setAttribute('aria-checked', String(enabled));
      localStorage.setItem(key, String(enabled));
      apply(enabled);
    };
    button.addEventListener('click', () => { enabled = !enabled; update(); });
    update();
  }

  setupToggle($('#updatesToggle'), 'playpods-updates', true, (enabled) => { state.updates = enabled; });
  setupToggle($('#motionToggle'), 'playpods-reduce-motion', false, (enabled) => document.body.classList.toggle('reduce-motion', enabled));
  $('#backendAddress').textContent = `${location.hostname || '127.0.0.1'}:${location.port || '18195'}`;

  document.addEventListener('keydown', (event) => {
    if (!['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key)) return;
    const focusable = $$('button:not([disabled])').filter((node) => node.offsetParent !== null);
    const current = Math.max(0, focusable.indexOf(document.activeElement));
    const direction = ['ArrowLeft', 'ArrowUp'].includes(event.key) ? -1 : 1;
    focusable[(current + direction + focusable.length) % focusable.length]?.focus();
    event.preventDefault();
  });

  refresh();
  setInterval(() => { if (state.updates) refresh(); }, 1200);
})();
