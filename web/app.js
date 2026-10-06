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
  const state = {
    status: null, devices: [], saved: [], updates: true, offline: false,
    polling: false, devicesRevision: -1, savedRevision: -1,
    devicesKey: '', savedKey: '', activeKey: ''
  };
  let toastTimer;

  const translations = {
    en: {
      brandSubtitle: 'PS5 Bluetooth audio', tabDevices: 'Devices', tabSaved: 'Saved devices', tabSettings: 'Settings',
      wirelessAudio: 'Wireless audio', heroTitle: 'Choose where your PS5 should play', scan: 'Scan', scanningAction: 'Scanning…',
      nearby: 'Nearby', bluetoothDevices: 'Bluetooth audio devices', noDevices: 'No devices yet',
      noDevicesHint: 'Put an A2DP/SBC device in pairing mode: headphones, an A2DP headset, speaker, soundbar, receiver or adapter.', quickReconnect: 'Quick reconnect',
      savedDevices: 'Saved devices', noSaved: 'No saved audio devices', noSavedHint: 'A Bluetooth audio device appears here after its first successful pairing.',
      preferences: 'Preferences', settings: 'Settings', automaticUpdates: 'Automatic updates',
      automaticUpdatesHint: 'Refresh connection state in the background.', reduceMotion: 'Reduce motion',
      reduceMotionHint: 'Turn off decorative interface animation.', nativeBackend: 'Native backend', release: 'Release',
      latencyProfile: 'Latency profile', latencyProfileHint: 'Low latency uses smaller RTP packets and trims stale queued audio more aggressively.',
      latencyLimit: 'It cannot remove buffering inside the Bluetooth audio device.', profileStable: 'Stable', profileLowLatency: 'Low latency',
      latencyDiagnostics: 'Stream diagnostics', latencyDiagnosticsHint: 'Measured in the capture, queue and RTP packetization path.',
      queueDisclaimer: 'Software queue only; Bluetooth device buffering is not included.',
      currentProfile: 'Current profile', nextProfile: 'Next connection', reconnectProfile: 'Disconnect and connect again to apply it.',
      packetDuration: 'RTP packet', queueDuration: 'Current queue', maxQueueDuration: 'Session max queue', queueTarget: 'Software target', trimmedAudio: 'Trimmed', frames: 'frames',
      captureOverruns: 'Capture overruns', packetRate: 'Packet rate', completionReports: 'Real completions', assumedCompletions: 'Assumed completions', missingReports: 'Missing reports', stallDuration: 'Stall', safetyStop: 'Safety stop', none: 'None', perSecond: 'pkt/s',
      navigate: 'Navigate', select: 'Select', streamFooter: 'AudioBridge streams with A2DP / SBC',
      online: 'Online', offline: 'Offline', found: (count) => `${count} found`, signal: 'Signal', saved: 'Saved',
      connect: 'Connect', disconnect: 'Disconnect', unknownDevice: 'Unknown device', currentDevice: 'Current device',
      requestFailed: (code) => `Request failed (${code})`, cannotReach: 'AudioBridge payload is not responding. Start it on the PS5; this page will keep waiting.',
      status: {
        starting: ['Starting', 'Preparing the Bluetooth controller…'], ready: ['Ready', 'Scan for a compatible Bluetooth audio device.'],
        scanning: ['Scanning', 'Looking for nearby audio devices…'], connecting: ['Connecting', 'Pairing and establishing a secure audio link…'],
        connected: ['Connected', 'Negotiating the A2DP audio stream…'], streaming: ['Streaming', 'PS5 audio is playing on the connected device.'],
        disconnecting: ['Disconnecting', 'Closing the audio stream safely…'], error: ['Needs attention', 'The backend reported a problem. Try again.'],
        stopped: ['Stopped', 'The native backend has stopped.'], unknown: ['Unknown', 'Waiting for backend status…']
      },
      api: {}
    },
    ru: {
      brandSubtitle: 'Bluetooth-аудио для PS5', tabDevices: 'Устройства', tabSaved: 'Сохранённые', tabSettings: 'Настройки',
      wirelessAudio: 'Беспроводной звук', heroTitle: 'Выберите, где должна звучать ваша PS5', scan: 'Поиск', scanningAction: 'Идёт поиск…',
      nearby: 'Рядом', bluetoothDevices: 'Bluetooth-аудиоустройства', noDevices: 'Устройств пока нет',
      noDevicesHint: 'Включите режим сопряжения на A2DP/SBC-устройстве: наушниках, гарнитуре в A2DP, колонке, саундбаре, ресивере или адаптере.', quickReconnect: 'Быстрое подключение',
      savedDevices: 'Сохранённые устройства', noSaved: 'Нет сохранённых аудиоустройств', noSavedHint: 'Bluetooth-аудиоустройство появится здесь после первого успешного сопряжения.',
      preferences: 'Параметры', settings: 'Настройки', automaticUpdates: 'Автообновление',
      automaticUpdatesHint: 'Обновлять состояние подключения в фоне.', reduceMotion: 'Уменьшить анимацию',
      reduceMotionHint: 'Отключить декоративные анимации интерфейса.', nativeBackend: 'Нативный backend', release: 'Версия',
      latencyProfile: 'Профиль задержки', latencyProfileHint: 'Low latency уменьшает RTP-пакеты и агрессивнее отбрасывает устаревший звук из очереди.',
      latencyLimit: 'Профиль не устраняет внутренний буфер Bluetooth-аудиоустройства.', profileStable: 'Стабильный', profileLowLatency: 'Low latency',
      latencyDiagnostics: 'Диагностика потока', latencyDiagnosticsHint: 'Измерения тракта захвата, очереди и RTP-пакетизации.',
      queueDisclaimer: 'Только программная очередь; внутренний буфер Bluetooth-устройства не учитывается.',
      currentProfile: 'Текущий профиль', nextProfile: 'При следующем подключении', reconnectProfile: 'Отключите и подключите устройство повторно.',
      packetDuration: 'RTP-пакет', queueDuration: 'Текущая очередь', maxQueueDuration: 'Макс. очередь сессии', queueTarget: 'Цель очереди', trimmedAudio: 'Отброшено', frames: 'кадров',
      captureOverruns: 'Переполнения захвата', packetRate: 'Скорость пакетов', completionReports: 'Реальные подтверждения', assumedCompletions: 'Предположенные', missingReports: 'Без отчёта', stallDuration: 'Остановка', safetyStop: 'Защитная остановка', none: 'Нет', perSecond: 'пак/с',
      navigate: 'Навигация', select: 'Выбрать', streamFooter: 'AudioBridge передаёт звук через A2DP / SBC',
      online: 'В сети', offline: 'Не в сети', found: (count) => `Найдено: ${count}`, signal: 'Сигнал', saved: 'Сохранено',
      connect: 'Подключить', disconnect: 'Отключить', unknownDevice: 'Неизвестное устройство', currentDevice: 'Текущее устройство',
      requestFailed: (code) => `Ошибка запроса (${code})`, cannotReach: 'Payload AudioBridge не отвечает. Запустите его на PS5 — эта страница продолжит ожидание.',
      status: {
        starting: ['Запуск', 'Подготовка Bluetooth-контроллера…'], ready: ['Готово', 'Найдите совместимое Bluetooth-аудиоустройство.'],
        scanning: ['Поиск', 'Поиск Bluetooth-аудиоустройств поблизости…'], connecting: ['Подключение', 'Сопряжение и установка защищённого аудиосоединения…'],
        connected: ['Подключено', 'Настройка аудиопотока A2DP…'], streaming: ['Трансляция', 'Звук PS5 воспроизводится на подключённом устройстве.'],
        disconnecting: ['Отключение', 'Безопасное завершение аудиопотока…'], error: ['Требуется внимание', 'Backend сообщил об ошибке. Попробуйте ещё раз.'],
        stopped: ['Остановлено', 'Нативный backend остановлен.'], unknown: ['Неизвестно', 'Ожидание состояния backend…']
      },
      api: {
        'Bluetooth scan queued': 'Поиск Bluetooth-устройств запущен', 'Backend is busy': 'Backend занят другой операцией',
        'Connection queued': 'Подключение поставлено в очередь', 'Unknown device or backend is busy': 'Устройство не найдено или backend занят',
        'Disconnect requested': 'Отключение запрошено', 'No active connection': 'Нет активного подключения',
        'Audio profile selected for the next stream': 'Профиль выбран для следующего подключения',
        'Profile is invalid': 'Некорректный профиль',
        'Bluetooth controller did not answer': 'Bluetooth-контроллер не ответил', 'Bluetooth connection failed': 'Не удалось подключиться по Bluetooth',
        'A2DP setup failed': 'Не удалось настроить A2DP', 'Unable to start Bluetooth scan': 'Не удалось запустить поиск Bluetooth'
      }
    }
  };

  let language = localStorage.getItem('audiobridge-language') ||
    ((navigator.language || '').toLowerCase().startsWith('ru') ? 'ru' : 'en');
  const t = (key) => translations[language][key];
  const translateMessage = (message) => translations[language].api[message] || message;

  async function api(path, options = {}) {
    const response = await fetch(path, {
      cache: 'no-store',
      headers: { 'Content-Type': 'application/json' },
      ...options
    });
    const data = await response.json();
    if (!response.ok) throw new Error(translateMessage(data.message) || t('requestFailed')(response.status));
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
    const rssi = device.rssi <= -127 ? `${t('signal')} —` : `${device.rssi} dBm`;
    card.innerHTML = `
      <div class="device-symbol" aria-hidden="true">◉</div>
      <div class="device-info">
        <h3></h3>
        <p class="device-meta"><span class="mac"></span><span>${rssi}</span>${device.saved ? `<span class="saved-badge">${t('saved')}</span>` : ''}</p>
      </div>
      <button class="device-action ${active ? 'disconnect' : ''}" ${busy && !active ? 'disabled' : ''}>${active ? t('disconnect') : t('connect')}</button>`;
    card.querySelector('h3').textContent = device.name || t('unknownDevice');
    card.querySelector('.mac').textContent = device.mac;
    card.querySelector('button').addEventListener('click', () => active ? disconnect() : connect(device.mac));
    if (savedView) card.dataset.saved = 'true';
    return card;
  }

  function renderStatus(status) {
    state.status = status;
    state.offline = false;
    document.body.dataset.backendState = status.status;
    const [label, message] = t('status')[status.status] || t('status').unknown;
    statusPill.className = `status-pill is-${status.status}`;
    statusPill.querySelector('span').textContent = label;
    statusMessage.textContent = status.error ? translateMessage(status.error) : message;
    scanButton.disabled = !['ready', 'error'].includes(status.status);
    scanButton.classList.toggle('is-loading', status.status === 'scanning');
    scanButton.querySelector('span:last-child').textContent = status.status === 'scanning' ? t('scanningAction') : t('scan');
    $('#controllerState').textContent = status.controller ? t('online') : t('offline');
    $('#deviceCount').textContent = t('found')(status.deviceCount);
    $$('[data-profile]').forEach((button) => {
      const selected = button.dataset.profile === status.selectedAudioProfile;
      button.classList.toggle('is-active', selected);
      button.setAttribute('aria-pressed', String(selected));
      button.disabled = false;
    });

    const latency = status.latency || {};
    const activeProfile = status.activeAudioProfile === 'low_latency' ? t('profileLowLatency') : t('profileStable');
    const selectedProfile = status.selectedAudioProfile === 'low_latency' ? t('profileLowLatency') : t('profileStable');
    const pending = status.streaming && status.activeAudioProfile !== status.selectedAudioProfile;
    $('#profileState').textContent = status.streaming
      ? `${t('currentProfile')}: ${activeProfile}. ${t('nextProfile')}: ${selectedProfile}.${pending ? ` ${t('reconnectProfile')}` : ''}`
      : `${t('nextProfile')}: ${selectedProfile}.`;
    $('#packetDuration').textContent = latency.packetDurationUs ? `${(latency.packetDurationUs / 1000).toFixed(1)} ms` : '—';
    $('#queueDuration').textContent = latency.streaming ? `${latency.queueMs} ms` : '—';
    $('#maxQueueDuration').textContent = latency.maxQueueMs ? `${latency.maxQueueMs} ms` : '—';
    $('#queueTarget').textContent = latency.targetMaxQueueMs ? `${latency.targetKeepQueueMs}–${latency.targetMaxQueueMs} ms` : '—';
    $('#trimmedAudio').textContent = latency.streaming || latency.trimmedFrames ? `${latency.trimmedFrames || 0} ${t('frames')}` : '—';
    $('#captureOverruns').textContent = latency.captureOverruns ?? '—';
    $('#packetRate').textContent = latency.streaming ? `${latency.packetSendRate || 0} ${t('perSecond')}` : '—';
    $('#completionReports').textContent = latency.completionReports ?? '—';
    $('#assumedCompletions').textContent = latency.assumedCompletions ?? '—';
    $('#missingReports').textContent = latency.missingReports ?? '—';
    $('#stallDuration').textContent = latency.stallMs ? `${latency.stallMs} ms` : '—';
    $('#safetyStop').textContent = latency.safetyStopReason || t('none');
    $('#bitpoolValue').textContent = latency.bitpool || '—';

    const activeKey = status.active ? `${status.active.mac}|${status.active.name}|${label}` : '';
    if (status.active && activeKey !== state.activeKey) {
      activeCard.classList.remove('is-hidden');
      activeCard.innerHTML = `<div><p class="eyebrow">${t('currentDevice')}</p><h3></h3><p>${status.active.mac} · ${label}</p></div><button class="device-action disconnect">${t('disconnect')}</button>`;
      activeCard.querySelector('h3').textContent = status.active.name;
      activeCard.querySelector('button').addEventListener('click', disconnect);
    } else if (!status.active) {
      activeCard.classList.add('is-hidden');
    }
    state.activeKey = activeKey;
  }

  function renderOffline() {
    state.offline = true;
    statusPill.className = 'status-pill is-error';
    statusPill.querySelector('span').textContent = t('offline');
    statusMessage.textContent = t('cannotReach');
    $('#controllerState').textContent = t('offline');
  }

  function renderDevices(devices) {
    const key = JSON.stringify(devices);
    if (key === state.devicesKey) return;
    state.devicesKey = key;
    state.devices = devices;
    deviceList.replaceChildren(...devices.map((device) => deviceCard(device)));
    $('#deviceEmpty').classList.toggle('is-hidden', devices.length > 0);
  }

  function renderSaved(devices) {
    const key = JSON.stringify(devices);
    if (key === state.savedKey) return;
    state.savedKey = key;
    state.saved = devices;
    savedList.replaceChildren(...devices.map((device) => deviceCard(device, true)));
    $('#savedEmpty').classList.toggle('is-hidden', devices.length > 0);
  }

  async function refresh(forceLists = false) {
    if (state.polling) return;
    state.polling = true;
    try {
      const status = await api('/api/status');
      const previousStatus = state.status?.status;
      renderStatus(status);
      const revisions = status.revisions || {};
      const scanEdge = previousStatus === 'scanning' || status.status === 'scanning';
      if (forceLists || scanEdge || revisions.devices !== state.devicesRevision) {
        const devices = await api('/api/devices');
        renderDevices(devices.devices);
        state.devicesRevision = revisions.devices;
      }
      if (forceLists || revisions.saved !== state.savedRevision) {
        const saved = await api('/api/saved');
        renderSaved(saved.devices);
        state.savedRevision = revisions.saved;
      }
    } catch (error) {
      renderOffline();
    } finally {
      state.polling = false;
    }
  }

  async function command(path, body) {
    try {
      const result = await api(path, { method: 'POST', body: body ? JSON.stringify(body) : '{}' });
      toast(translateMessage(result.message));
      await refresh(true);
    } catch (error) {
      toast(error.message);
    }
  }

  const connect = (mac) => command('/api/connect', { mac });
  const disconnect = () => command('/api/disconnect');
  scanButton.addEventListener('click', () => command('/api/scan'));
  $$('[data-profile]').forEach((button) => button.addEventListener('click', () =>
    command('/api/audio-profile', { profile: button.dataset.profile })));

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

  function applyLanguage() {
    document.documentElement.lang = language;
    $$('[data-i18n]').forEach((node) => { node.textContent = t(node.dataset.i18n); });
    $$('[data-language]').forEach((button) => button.classList.toggle('is-active', button.dataset.language === language));
    localStorage.setItem('audiobridge-language', language);
    if (state.status) renderStatus(state.status);
    else if (state.offline) renderOffline();
    renderDevices(state.devices);
    renderSaved(state.saved);
  }

  $$('[data-language]').forEach((button) => button.addEventListener('click', () => {
    language = button.dataset.language;
    applyLanguage();
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

  setupToggle($('#updatesToggle'), 'audiobridge-updates', true, (enabled) => { state.updates = enabled; });
  setupToggle($('#motionToggle'), 'audiobridge-reduce-motion', false, (enabled) => document.body.classList.toggle('reduce-motion', enabled));
  $('#backendAddress').textContent = `${location.hostname || '127.0.0.1'}:${location.port || '18195'}`;

  document.addEventListener('keydown', (event) => {
    if (!['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key)) return;
    const focusable = $$('button:not([disabled])').filter((node) => node.offsetParent !== null);
    const current = Math.max(0, focusable.indexOf(document.activeElement));
    const direction = ['ArrowLeft', 'ArrowUp'].includes(event.key) ? -1 : 1;
    focusable[(current + direction + focusable.length) % focusable.length]?.focus();
    event.preventDefault();
  });

  applyLanguage();
  refresh();
  const poll = async () => {
    if (state.updates) await refresh();
    setTimeout(poll, 1200);
  };
  setTimeout(poll, 1200);
})();
