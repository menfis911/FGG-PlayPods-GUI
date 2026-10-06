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

  const translations = {
    en: {
      brandSubtitle: 'PS5 Bluetooth audio', tabDevices: 'Devices', tabSaved: 'Saved devices', tabSettings: 'Settings',
      wirelessAudio: 'Wireless audio', heroTitle: 'Choose where your PS5 should play', scan: 'Scan', scanningAction: 'Scanning…',
      nearby: 'Nearby', bluetoothDevices: 'Bluetooth devices', noDevices: 'No devices yet',
      noDevicesHint: 'Put your headphones in pairing mode, then choose Scan.', quickReconnect: 'Quick reconnect',
      savedDevices: 'Saved devices', noSaved: 'No saved headphones', noSavedHint: 'A device appears here after its first successful pairing.',
      preferences: 'Preferences', settings: 'Settings', automaticUpdates: 'Automatic updates',
      automaticUpdatesHint: 'Refresh connection state in the background.', reduceMotion: 'Reduce motion',
      reduceMotionHint: 'Turn off decorative interface animation.', nativeBackend: 'Native backend', release: 'Release',
      navigate: 'Navigate', select: 'Select', streamFooter: 'AudioBridge streams with A2DP / SBC',
      online: 'Online', offline: 'Offline', found: (count) => `${count} found`, signal: 'Signal', saved: 'Saved',
      connect: 'Connect', disconnect: 'Disconnect', unknownDevice: 'Unknown device', currentDevice: 'Current device',
      requestFailed: (code) => `Request failed (${code})`, cannotReach: 'Cannot reach the native backend.',
      status: {
        starting: ['Starting', 'Preparing the Bluetooth controller…'], ready: ['Ready', 'Scan for nearby Bluetooth headphones and speakers.'],
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
      nearby: 'Рядом', bluetoothDevices: 'Bluetooth-устройства', noDevices: 'Устройств пока нет',
      noDevicesHint: 'Переведите наушники в режим сопряжения и нажмите «Поиск».', quickReconnect: 'Быстрое подключение',
      savedDevices: 'Сохранённые устройства', noSaved: 'Нет сохранённых наушников', noSavedHint: 'Устройство появится здесь после первого успешного сопряжения.',
      preferences: 'Параметры', settings: 'Настройки', automaticUpdates: 'Автообновление',
      automaticUpdatesHint: 'Обновлять состояние подключения в фоне.', reduceMotion: 'Уменьшить анимацию',
      reduceMotionHint: 'Отключить декоративные анимации интерфейса.', nativeBackend: 'Нативный backend', release: 'Версия',
      navigate: 'Навигация', select: 'Выбрать', streamFooter: 'AudioBridge передаёт звук через A2DP / SBC',
      online: 'В сети', offline: 'Не в сети', found: (count) => `Найдено: ${count}`, signal: 'Сигнал', saved: 'Сохранено',
      connect: 'Подключить', disconnect: 'Отключить', unknownDevice: 'Неизвестное устройство', currentDevice: 'Текущее устройство',
      requestFailed: (code) => `Ошибка запроса (${code})`, cannotReach: 'Нет связи с нативным backend.',
      status: {
        starting: ['Запуск', 'Подготовка Bluetooth-контроллера…'], ready: ['Готово', 'Найдите Bluetooth-наушники или колонку поблизости.'],
        scanning: ['Поиск', 'Поиск Bluetooth-аудиоустройств поблизости…'], connecting: ['Подключение', 'Сопряжение и установка защищённого аудиосоединения…'],
        connected: ['Подключено', 'Настройка аудиопотока A2DP…'], streaming: ['Трансляция', 'Звук PS5 воспроизводится на подключённом устройстве.'],
        disconnecting: ['Отключение', 'Безопасное завершение аудиопотока…'], error: ['Требуется внимание', 'Backend сообщил об ошибке. Попробуйте ещё раз.'],
        stopped: ['Остановлено', 'Нативный backend остановлен.'], unknown: ['Неизвестно', 'Ожидание состояния backend…']
      },
      api: {
        'Bluetooth scan queued': 'Поиск Bluetooth-устройств запущен', 'Backend is busy': 'Backend занят другой операцией',
        'Connection queued': 'Подключение поставлено в очередь', 'Unknown device or backend is busy': 'Устройство не найдено или backend занят',
        'Disconnect requested': 'Отключение запрошено', 'No active connection': 'Нет активного подключения',
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

    if (status.active) {
      activeCard.classList.remove('is-hidden');
      activeCard.innerHTML = `<div><p class="eyebrow">${t('currentDevice')}</p><h3></h3><p>${status.active.mac} · ${label}</p></div><button class="device-action disconnect">${t('disconnect')}</button>`;
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
      statusPill.querySelector('span').textContent = t('offline');
      statusMessage.textContent = t('cannotReach');
      $('#controllerState').textContent = t('offline');
    }
  }

  async function command(path, body) {
    try {
      const result = await api(path, { method: 'POST', body: body ? JSON.stringify(body) : '{}' });
      toast(translateMessage(result.message));
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

  function applyLanguage() {
    document.documentElement.lang = language;
    $$('[data-i18n]').forEach((node) => { node.textContent = t(node.dataset.i18n); });
    $$('[data-language]').forEach((button) => button.classList.toggle('is-active', button.dataset.language === language));
    localStorage.setItem('audiobridge-language', language);
    if (state.status) renderStatus(state.status);
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
  setInterval(() => { if (state.updates) refresh(); }, 1200);
})();
