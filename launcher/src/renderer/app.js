const api = window.launcher;
const $ = selector => document.querySelector(selector);
let state, page = 'welcome', toastTimer, scanning = false, selectedSkin = 0, skinPage = 0;
let storySelection = Promise.resolve();
const names = { welcome: 'Почати гру', host: 'Створення лобі', home: 'Головна', lobby: 'Локальне лобі', files: 'Файли гри', settings: 'Налаштування' };
const navIcons = {
  welcome: ['M7 7h10a4 4 0 0 1 4 4l1 6a2 2 0 0 1-3 2l-4-3H9l-4 3a2 2 0 0 1-3-2l1-6a4 4 0 0 1 4-4Z', 'M7 10v4M5 12h4M16 11h.01M19 13h.01'],
  lobby: ['M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2M17 4a4 4 0 0 1 0 8M22 21v-2a4 4 0 0 0-3-3.87', 'M13 7a4 4 0 1 1-8 0 4 4 0 0 1 8 0Z'],
  files: ['M12 3 3 8v9l9 5 9-5V8l-9-5ZM3 8l9 5 9-5M12 13v9M7 5.8l9 5'],
  settings: ['M9 3h6l1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1 1-3Z', 'M16 12a4 4 0 1 1-8 0 4 4 0 0 1 8 0Z'],
};
for (const button of document.querySelectorAll('.nav')) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('aria-hidden', 'true');
  for (const d of navIcons[button.dataset.page] || []) {
    const part = document.createElementNS('http://www.w3.org/2000/svg', 'path'); part.setAttribute('d', d); svg.append(part);
  }
  button.querySelector('.icon').replaceChildren(svg);
}
function updateClock() { $('#header-clock').textContent = new Intl.DateTimeFormat('uk-UA', { hour: '2-digit', minute: '2-digit' }).format(new Date()); }
updateClock(); setInterval(updateClock, 30000);
function toast(message, persistent = false) {
  clearTimeout(toastTimer); $('#toast-text').textContent = message; $('#toast').hidden = false;
  if (!persistent) toastTimer = setTimeout(() => { $('#toast').hidden = true; }, 8000);
}
function go(next) {
  page = next;
  document.querySelectorAll('.page').forEach(el => el.classList.toggle('active', el.id === 'page-' + next));
  document.querySelectorAll('.nav').forEach(el => el.classList.toggle('active', el.dataset.page === next));
  $('#breadcrumb').textContent = names[next];
  if (next === 'host') prepareHost();
  if (next === 'lobby' && !state.room) scan();
}
async function action(button, work) {
  if (button) button.disabled = true;
  try { return await work(); }
  catch (error) { toast(error.message.replace(/^Error invoking remote method '[^']+': Error: /, ''), true); }
  finally { if (button) button.disabled = false; render(); }
}
function populate() {
  const form = $('#settings-form');
  for (const [key, value] of Object.entries(state.settings)) {
    const input = form.elements.namedItem(key);
    if (!input) continue;
    if (input.type === 'checkbox') input.checked = value;
    else input.value = value;
  }
  updateRanges();
}
function updateRanges() {
  $('#voice-value').textContent = $('#settings-form').elements.voiceVolume.value + '%';
  $('#mic-value').textContent = $('#settings-form').elements.micGain.value + '%';
}
function render() {
  if (!state) return;
  const s = state.settings, game = state.game;
  $('#profile-name').textContent = s.nickname;
  $('#profile-initial').textContent = s.nickname[0].toUpperCase();
  $('#welcome-name').textContent = s.nickname;
  $('#welcome-avatar').textContent = s.nickname[0].toUpperCase();
  $('#welcome-skin').textContent = state.skins.find(skin => skin.id === s.skin)?.name || 'CJ';
  $('#welcome-game-status').textContent = game.installed && game.supported ? '✓ GTA та мод готові. Можна створити лобі або приєднатись.' : 'Оберіть GTA та встановіть мод перед запуском.';
  const me = state.room?.members.find(m => m.id === state.selfId);
  $('#header-player-count').textContent = state.room?.members.length || 0;
  $('#header-player-meter').value = state.room?.members.length || 0;
  $('#header-session-label').textContent = state.room?.status === 'running' ? 'сесія триває' : 'гравців у лобі';
  $('#header-profile').disabled = state.playing || state.room?.status === 'starting';
  $('#server-settings').disabled = !!state.room && (!me?.host || state.room.status !== 'waiting');
  $('#refresh-lobbies').hidden = !!state.room;
  $('#launcher-version').textContent = state.version;
  $('#platform-label').textContent = state.platform === 'linux' ? 'LINUX / WINE' : 'WINDOWS / NATIVE';
  $('#files-path').textContent = s.gameDir || 'Оберіть папку встановленої GTA';
  const status = $('#game-status');
  status.textContent = state.playing ? 'Гра запущена' : game.installed && game.supported ? 'Готова до гри' : game.found ? game.supported ? 'Потрібен мод' : 'Інша версія GTA' : 'Не налаштовано';
  status.classList.toggle('good', game.installed && game.supported);
  $('#compatibility').textContent = !game.found ? 'Очікуємо на папку з gta_sa.exe.' : game.supported ? '✓ GTA 1.0 US знайдено. Можна встановлювати мод.' : 'Версія gta_sa.exe не підтримується. Потрібна GTA 1.0 US, 14 383 616 байтів.';
  for (const id of ['install-mod', 'install-local']) $( '#' + id).disabled = !game.supported || state.busy || state.playing;
  $('#choose-game').disabled = state.busy || state.playing || !!state.room;
  $('#install-progress').hidden = !state.busy;
  if (state.release) showRelease(state.release);
  renderRoom();
}
function showRelease(release) {
  $('#release-name').textContent = release.name;
  $('#release-details').textContent = `${release.version} · ${(release.size / 1048576).toFixed(1)} МБ · ${state.settings.releaseRepo}`;
}
function renderRoom() {
  const room = state.room;
  $('#lobby-browser').hidden = !!room; $('#active-room').hidden = !room;
  if (!room) return;
  const me = room.members.find(m => m.id === state.selfId);
  $('#room-name').textContent = room.settings.name;
  $('#room-details').textContent = `${room.members.length} / ${room.settings.maxPlayers} гравців · ${room.settings.friendlyFire ? 'Дружній вогонь увімкнено' : 'Без дружнього вогню'} · ${room.settings.loadSlot < 0 ? 'Нова гра' : room.settings.loadSlot === 0 ? 'Вибір збереження в GTA' : 'Збереження ' + room.settings.loadSlot}`;
  $('#room-state').textContent = { waiting: 'ЛОББІ ВІДКРИТО', starting: 'ЗАПУСКАЄМО СЕРВЕР…', running: 'СЕСІЯ ТРИВАЄ' }[room.status] || 'ЛОББІ';
  const startButton = $('#start-session'), readyButton = $('#ready-button');
  const members = $('#members'); members.replaceChildren(); members.dataset.count = room.members.length;
  for (const player of room.members) {
    const own = player.id === state.selfId, skin = state.skins.find(s => s.id === player.skin) || state.skins[0];
    const el = document.createElement('div');
    el.className = 'member' + (own ? ' player-self' : '') + (player.ready ? ' ready' : '') + (player.playing ? ' playing' : '');
    const top = document.createElement('div'); top.className = 'member-top';
    const role = document.createElement('span'); role.textContent = player.host ? '★ HOST' : 'CREW';
    const self = document.createElement('span'); self.textContent = own ? 'ВАШ ПЕРСОНАЖ' : '';
    top.append(role, self);
    const portrait = document.createElement('div'); portrait.className = 'character-preview';
    const image = document.createElement('img'); image.src = `../../assets/skins/${skin.id}.png`; image.alt = skin.name; portrait.append(image);
    const name = document.createElement('strong'); name.className = 'member-name'; name.textContent = player.name;
    const skinName = document.createElement('p'); skinName.className = 'skin-name'; skinName.textContent = skin.name;
    el.append(top, portrait, name, skinName);
    if (own) {
      const controls = document.createElement('div'); controls.className = 'member-controls';
      const edit = document.createElement('button'); edit.className = 'text-button edit-character'; edit.textContent = 'Змінити нік та скін ↗';
      edit.disabled = state.playing || player.launching || room.status === 'starting'; edit.addEventListener('click', openProfile);
      controls.append(edit, player.host ? startButton : readyButton); el.append(controls);
    } else {
      const status = document.createElement('p'); status.className = 'member-status';
      status.textContent = player.playing ? '● На сервері' : player.launching ? 'Підключається…' : player.ready ? '✓ Готовий' : 'Обирає персонажа';
      el.append(status);
    }
    members.append(el);
  }
  if (!members.contains(startButton)) { startButton.hidden = true; $('#room-bottom-anchor').append(startButton); }
  if (!members.contains(readyButton)) { readyButton.hidden = true; $('#room-bottom-anchor').append(readyButton); }
  const ready = room.members.filter(m => m.ready).length;
  $('#ready-summary').textContent = `${ready} з ${room.members.length} готові`;
  $('#ready-button').hidden = !!me?.host;
  $('#ready-button').textContent = me?.playing ? '● Ви на сервері' : me?.launching ? 'Підключаємося до сервера…' : state.playing ? 'Закрийте GTA для повторного входу' : room.status === 'running' ? 'Приєднатися до гри ↗' : me?.ready ? '✓ Готовий · скасувати' : 'Я готовий';
  $('#ready-button').disabled = room.status === 'starting' || state.playing || me?.launching || state.busy;
  $('#start-session').hidden = !me?.host;
  $('#start-session').disabled = room.status !== 'waiting' || ready !== room.members.length || state.playing || state.busy;
  $('#start-session').textContent = room.status === 'starting' ? 'Сервер запускається…' : room.status === 'running' ? 'Сесія триває' : 'Почати сесію ↗';
  $('#leave-room').disabled = state.playing;
  $('#room-server-settings').hidden = !me?.host;
  $('#room-server-settings').disabled = room.status !== 'waiting';
  $('#room-hint').textContent = room.status === 'running' ? 'Сесія триває. Нові учасники можуть обрати персонажа й приєднатися до гри.' : 'Оберіть свій скін та нік. Коли всі будуть готові, хост зможе почати сесію.';
}
async function scan() {
  if (scanning || state?.room) return;
  scanning = true; $('#refresh-lobbies').disabled = true; $('#refresh-lobbies').textContent = '↻ Шукаємо…';
  try {
    const rooms = await api.discover();
    $('#lobby-count').textContent = rooms.length;
    const list = $('#room-list'); list.replaceChildren();
    if (!rooms.length) {
      const empty = document.createElement('div'); empty.className = 'empty';
      const icon = document.createElement('span'); icon.className = 'empty-icon'; icon.textContent = '◎';
      const title = document.createElement('h3'); title.textContent = 'Поки що тихо на районі';
      const hint = document.createElement('p'); hint.textContent = 'Створіть лоббі та запросіть своїх. Або введіть адресу друга нижче.';
      empty.append(icon, title, hint); list.append(empty);
    }
    for (const room of rooms) {
      const row = document.createElement('div'); row.className = 'room-item';
      const icon = document.createElement('span'); icon.className = 'room-icon'; icon.textContent = '◎';
      const detail = document.createElement('div');
      const name = document.createElement('h3'); name.textContent = room.name;
      const info = document.createElement('p'); info.textContent = `${room.count} / ${room.maxPlayers} гравців · ${room.locked ? 'З паролем' : 'Відкрите лоббі'} · ${room.status === 'waiting' ? 'Збираємо банду' : 'Сесія триває'}`;
      detail.append(name, info);
      const button = document.createElement('button'); button.className = 'btn outline'; button.textContent = 'Приєднатись →';
      button.disabled = room.status === 'starting' || room.count >= room.maxPlayers;
      button.addEventListener('click', () => {
        $('#join-address').value = room.address;
        if (room.locked) { $('#join-password').focus(); toast('Введіть пароль лоббі та натисніть «Приєднатись».'); }
        else $('#join-form').requestSubmit();
      });
      row.append(icon, detail, button); list.append(row);
    }
  } catch (error) { toast(error.message); }
  finally { scanning = false; $('#refresh-lobbies').disabled = false; $('#refresh-lobbies').textContent = '↻ Оновити'; }
}
async function host(button) {
  if (state.room) return go('lobby');
  if (!state.game.supported || !state.game.installed) { go('files'); toast('Спочатку оберіть GTA та встановіть мод.'); return; }
  go('host');
}
function prepareHost() {
  $('#host-name').value = state.settings.serverName;
  $('#host-network').replaceChildren();
  for (const address of state.addresses) {
    const option = document.createElement('option'); option.value = address; option.textContent = address;
    $('#host-network').append(option);
  }
  if (!state.addresses.length) {
    const option = document.createElement('option'); option.value = ''; option.textContent = 'Автоматично'; $('#host-network').append(option);
  }
  if (state.settings.networkAddress) $('#host-network').value = state.settings.networkAddress;
}
function openProfile() {
  const player = state.room?.members.find(m => m.id === state.selfId);
  $('#player-name').value = player?.name || state.settings.nickname;
  selectedSkin = player?.skin ?? state.settings.skin;
  $('#remember-profile').checked = !player || (player.name === state.settings.nickname && player.skin === state.settings.skin);
  $('#skin-search').value = ''; $('#skin-category').value = '';
  skinPage = Math.max(0, Math.floor(state.skins.findIndex(s => s.id === selectedSkin) / 12));
  renderSkins(); $('#profile-dialog').showModal();
}
function renderSkins() {
  $('#skin-picker').replaceChildren();
  const query = $('#skin-search').value.trim().toLowerCase(), category = $('#skin-category').value;
  const filtered = state.skins.filter(s => (!category || s.description === category) && `${s.id} ${s.name} ${s.model}`.toLowerCase().includes(query));
  const pages = Math.max(1, Math.ceil(filtered.length / 12));
  skinPage = Math.max(0, Math.min(skinPage, pages - 1));
  $('#skin-page').textContent = `${skinPage + 1} / ${pages} · ${filtered.length} скінів`;
  $('#skin-prev').disabled = skinPage === 0; $('#skin-next').disabled = skinPage + 1 === pages;
  if (!filtered.length) { const empty = document.createElement('p'); empty.textContent = 'Нічого не знайдено'; $('#skin-picker').append(empty); }
  for (const skin of filtered.slice(skinPage * 12, skinPage * 12 + 12)) {
    const button = document.createElement('button'); button.type = 'button';
    button.className = 'skin-option' + (skin.id === selectedSkin ? ' selected' : '');
    button.setAttribute('aria-pressed', String(skin.id === selectedSkin)); button.dataset.skin = skin.id;
    const image = document.createElement('img'); image.src = `../../assets/skins/${skin.id}.png`; image.alt = skin.name;
    const label = document.createElement('span'); label.textContent = `#${skin.id} · ${skin.name}`; button.append(image, label);
    button.addEventListener('click', () => { selectedSkin = skin.id; renderSkins(); }); $('#skin-picker').append(button);
  }
}
$('#skin-search').addEventListener('input', () => { skinPage = 0; renderSkins(); });
$('#skin-category').addEventListener('change', () => { skinPage = 0; renderSkins(); });
$('#skin-prev').addEventListener('click', () => { skinPage--; renderSkins(); });
$('#skin-next').addEventListener('click', () => { skinPage++; renderSkins(); });

document.querySelectorAll('[data-page]').forEach(button => button.addEventListener('click', () => go(button.dataset.page)));
document.querySelectorAll('[data-go]').forEach(button => button.addEventListener('click', () => go(button.dataset.go)));
$('.brand').addEventListener('click', event => { event.preventDefault(); go('welcome'); });
$('#choose-host').addEventListener('click', () => state.room ? go('lobby') : go('host'));
$('#choose-join').addEventListener('click', () => go('lobby'));
$('#host-form').addEventListener('submit', event => {
  event.preventDefault(); action($('#host-form [type="submit"]'), async () => {
    await storySelection;
    state = await api.save({ ...state.settings, serverName: $('#host-name').value, networkAddress: $('#host-network').value });
    state = await api.host(); populate(); go('lobby');
  });
});
$('#edit-global-profile').addEventListener('click', openProfile);
$('#header-profile').addEventListener('click', openProfile);
$('#close-profile').addEventListener('click', () => $('#profile-dialog').close());
$('#profile-form').addEventListener('submit', event => {
  event.preventDefault(); action($('#profile-form [type="submit"]'), async () => {
    state = await api.profile({ name: $('#player-name').value, skin: selectedSkin, remember: $('#remember-profile').checked });
    populate(); $('#profile-dialog').close();
  });
});
$('#room-server-settings').addEventListener('click', () => go('settings'));
$('#create-lobby').addEventListener('click', e => host(e.currentTarget));
$('#refresh-lobbies').addEventListener('click', scan);
$('#dismiss-toast').addEventListener('click', () => { $('#toast').hidden = true; });
$('#choose-game').addEventListener('click', e => action(e.currentTarget, async () => { state = await api.chooseGame(); populate(); }));
$('#open-folder').addEventListener('click', e => action(e.currentTarget, () => api.openFolder()));
$('#open-releases').addEventListener('click', () => action(null, () => api.openReleases()));
$('#check-release').addEventListener('click', e => action(e.currentTarget, async () => { state.release = await api.release(); }));
for (const [id, local] of [['install-mod', false], ['install-local', true]])
  $('#' + id).addEventListener('click', e => action(e.currentTarget, async () => { state = await api.install(local); toast('Мод встановлено. Час збирати друзів!'); }));
$('#cancel-install').addEventListener('click', () => api.cancelInstall());
$('#join-form').addEventListener('submit', e => {
  e.preventDefault(); action($('#join-form button'), async () => {
    let address = $('#join-address').value.trim();
    if (!address.includes('/')) {
      const found = await api.discover(address);
      if (!found.length) throw new Error('Лобі за цією IP-адресою не знайдено. Перевірте мережу та брандмауер.');
      address = found[0].address;
    }
    state = await api.join(address, $('#join-password').value); $('#join-password').value = '';
  });
});
$('#leave-room').addEventListener('click', e => action(e.currentTarget, async () => { state = await api.leave(); scan(); }));
$('#ready-button').addEventListener('click', e => action(e.currentTarget, () => {
  const me = state.room.members.find(m => m.id === state.selfId); return api.ready(!me?.ready);
}));
$('#start-session').addEventListener('click', e => action(e.currentTarget, async () => { await storySelection; return api.start(); }));
$('#copy-invite').addEventListener('click', e => action(e.currentTarget, async () => { const invite = await api.copyInvite(); toast('Запрошення скопійовано: ' + invite); }));
$('#settings-form').addEventListener('input', updateRanges);
// Choosing a story start takes effect immediately, including in an existing lobby.
$('#settings-form [name="loadSlot"]').addEventListener('change', e => {
  const input = e.currentTarget, loadSlot = Number(input.value);
  storySelection = api.save({ ...state.settings, loadSlot }).then(value => { state = value; render(); })
    .catch(error => { input.value = state.settings.loadSlot; throw error; });
  action(input, () => storySelection);
});
$('#settings-form').addEventListener('submit', e => {
  e.preventDefault(); action(document.querySelector('[form="settings-form"]'), async () => {
    await storySelection.catch(() => {});
    const values = { ...state.settings };
    for (const input of e.target.elements) {
      if (!input.name) continue;
      values[input.name] = input.type === 'checkbox' ? input.checked : typeof state.settings[input.name] === 'number' ? Number(input.value) : input.value;
    }
    state = await api.save(values); populate(); toast('Налаштування збережено.');
  });
});
api.onEvent(event => {
  if (!state) return;
  if (event.type === 'lobby') { state.room = event.room; state.selfId = event.selfId; }
  else if (event.type === 'busy') state.busy = event.busy;
  else if (event.type === 'game') state.playing = event.playing;
  else if (event.type === 'notice') toast(event.message, true);
  else if (event.type === 'progress') {
    const text = { download: 'Завантаження мода', extract: 'Перевіряємо та розпаковуємо', install: 'Встановлюємо файли', done: 'Мод готовий до гри' }[event.stage];
    const percent = event.total ? Math.round(event.received / event.total * 100) : null;
    $('#progress-text').textContent = text + (percent === null ? '…' : ` · ${percent}%`);
    if (percent !== null) $('#progress-bar').value = percent;
    else if (event.stage === 'done') $('#progress-bar').value = 100;
    else $('#progress-bar').removeAttribute('value');
  }
  render();
});
api.state().then(value => { state = value; populate(); go('welcome'); render(); }).catch(error => toast(error.message, true));
setInterval(() => { if (page === 'lobby' && !state?.room) scan(); }, 8000);
