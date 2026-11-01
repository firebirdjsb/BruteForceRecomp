'use strict';
const $ = id => document.getElementById(id);
const token = location.hash.slice(1) || sessionStorage.getItem('launcher-session') || '';
if (token) sessionStorage.setItem('launcher-session', token);
// Keep the session secret out of browser history and referrers after loading.
history.replaceState(null, '', '/');
const labels = {empty:'AWAITING DISC',importing:'IMPORT IN PROGRESS',verifying:'VERIFYING FILES',imported:'DISC IMPORTED',ready:'READY TO DEPLOY',running:'SQUAD DEPLOYED',error:'NEEDS ATTENTION'};
let pending = false;
let latest = null;
let connected = false;

async function api(path, data) {
  const response = await fetch('/api/' + path, {method:data ? 'POST' : 'GET',
    headers:{'X-Launcher-Token':token,...(data ? {'Content-Type':'application/json'} : {})},
    body:data ? JSON.stringify(data) : undefined, cache:'no-store'});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'The command center could not complete this action.');
  return result;
}
function render(state) {
  latest = state;
  const busy = pending || ['importing','verifying','running'].includes(state.phase);
  $('badge').textContent = labels[state.phase] || state.phase.toUpperCase();
  $('message').textContent = state.message;
  $('progress').value = state.progress;
  $('percent').textContent = ['importing','verifying','ready','imported'].includes(state.phase) ? Math.round(state.progress) + '%' : '—';
  $('phase-label').textContent = state.phase === 'importing' ? 'EXTRACTING GAME DATA' : 'MISSION STATUS';
  $('current-file').textContent = state.current_file;
  $('error').textContent = state.error;
  $('error').hidden = !state.error;
  $('browse').disabled = busy;
  $('import').disabled = busy || !$('iso').value.trim();
  $('iso').disabled = busy;
  $('verify').disabled = busy || !state.game_dir || !state.native_available;
  $('keyboard').disabled = state.running;
  $('play').disabled = pending || state.phase !== 'ready' || !state.native_available;
  $('host-path').textContent = state.executable || 'No native executable selected. See README for launcher setup.';
  $('game-path').textContent = state.game_dir || 'No disc imported';
  $('log-path').textContent = state.log || 'No session yet';
  $('file-count').textContent = state.imported_files ? state.imported_files.toLocaleString() + ' FILES IN LOCAL LIBRARY' : '';
  const step = ['ready','running'].includes(state.phase) ? 'play' : ['importing','verifying','imported'].includes(state.phase) ? 'import' : 'disc';
  for (const name of ['disc','import','play']) $('step-' + name).classList.toggle('active', name === step);
}
async function action(name, data={}) {
  pending = true;
  if (latest) render(latest);
  try {
    const result = await api(name, data);
    if (name === 'pick' && result.path) $('iso').value = result.path;
    connected = true;
  } catch (error) {
    $('error').textContent = error.message;
    $('error').hidden = false;
    // Keep an action error visible until the next action, instead of having
    // the status poll erase it immediately.
    if (latest) latest.error = error.message;
    action.error = error.message;
  } finally {
    pending = false;
    if (latest) render({...latest,error:action.error || latest.error});
  }
}
$('browse').addEventListener('click', () => { action.error=''; action('pick'); });
$('import').addEventListener('click', () => { action.error=''; action('import',{path:$('iso').value.trim()}); });
$('verify').addEventListener('click', () => { action.error=''; action('verify'); });
$('play').addEventListener('click', () => { action.error=''; action('launch',{keyboard:$('keyboard').checked}); });
$('iso').addEventListener('input', () => { if(latest) render({...latest,error:action.error || latest.error}); });
// No duplicate network requests while a slow status poll is outstanding.
async function poll() {
  try {
    const state = await api('state');
    connected = true;
    render({...state,error:action.error || state.error});
  } catch (error) {
    connected = false;
    $('badge').textContent = 'UPLINK OFFLINE';
    $('message').textContent = token ? 'The local launcher is unavailable. Reopen it from your terminal.' : 'Open this page from the launcher session link printed in your terminal.';
    $('error').textContent = error.message;
    $('error').hidden = false;
    for (const id of ['browse','import','verify','play']) $(id).disabled = true;
  }
  setTimeout(poll, connected ? 700 : 2000);
}
poll();
