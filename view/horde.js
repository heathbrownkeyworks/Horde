// Horde follower command, hosted by Magelight UI.
// Native pushes state through hordeUpdateState(state); the page sends orders
// through the listeners HordeUI registers (magelight.send('hordeSetWait', json)).
(function () {
    'use strict';

    const STAGE_W = 1600, STAGE_H = 900;
    const MUSTER_MAX_METERS = 2000;
    const $ = id => document.getElementById(id);
    const esc = v => String(v == null ? '' : v).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
    const num = (v, fallback = 0) => (typeof v === 'number' && isFinite(v) ? v : fallback);
    const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
    const ratio = (v, max) => (num(max) > 0 ? clamp(num(v) / num(max), 0, 1) : 0);
    const easeOut = t => 1 - Math.pow(1 - t, 3);

    // ── Icons ────────────────────────────────────────────────────────────
    const ICONS = window.HordeIcons || { lucide: {}, game: {} };
    function icon(name) {
        const nodes = ICONS.lucide[name];
        if (!nodes) return '';
        const inner = nodes.map(([tag, attrs]) => '<' + tag + Object.keys(attrs).filter(k => k !== 'key')
            .map(k => ' ' + k + '="' + esc(attrs[k]) + '"').join('') + '/>').join('');
        return '<svg class="i" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' + inner + '</svg>';
    }
    function glyph(name) {
        const paths = ICONS.game[name];
        return paths ? '<svg class="g" viewBox="0 0 512 512">' + paths.map(d => '<path fill="currentColor" d="' + d + '"/>').join('') + '</svg>' : '';
    }
    function hydrateIcons(root) {
        root.querySelectorAll('[data-icon]').forEach(el => {
            el.outerHTML = icon(el.getAttribute('data-icon'));
        });
    }

    // ── State ────────────────────────────────────────────────────────────
    const model = {
        loaded: false, followers: [], dismissed: [], dismissedCount: 0, maxFollowers: 20, allPassive: false,
        followDistance: 'normal', notificationsEnabled: true, useKeybinds: true, defaultSandboxEnabled: false
    };
    const ui = { open: false, tab: 'followers', selected: 0, sort: 'recruited', modal: null, afterState: [], arrive: false, zoom: 1, scale: 1 };

    function send(name, payload) {
        const host = window.magelight;
        if (host && typeof host.send === 'function') {
            host.send(name, JSON.stringify(payload || {}));
            return true;
        }
        console.warn('Horde: native action unavailable: ' + name);
        return false;
    }
    // Feedback waits for native to confirm the order with a state push. When an
    // expectation is given, the toast waits until the state shows it (or gives up).
    function order(name, payload, confirm, expect) {
        if (!send(name, payload) || !confirm) return;
        let tries = 3;
        const check = () => {
            if (!expect || expect()) confirm();
            else if (--tries > 0) ui.afterState.push(check);
        };
        ui.afterState.push(check);
    }

    // navigator.language does not follow the game's language, so native sends
    // it as a BCP 47 tag with each state push. Names sort and numbers read in it.
    let collator, kmFormat;
    function setLanguage(tag) {
        try {
            collator = new Intl.Collator(tag, { sensitivity: 'base', numeric: true });
            kmFormat = [0, 1].map(digits => new Intl.NumberFormat(tag, { minimumFractionDigits: digits, maximumFractionDigits: digits }));
            document.documentElement.lang = tag;
        } catch (error) {
            console.warn('Horde: unsupported language ' + tag);
        }
    }
    setLanguage('en');

    const followerById = id => model.followers.find(f => f.formID === id);
    const dismissedById = id => model.dismissed.find(d => d.formID === id);
    const isLit = f => !f.isWaiting;
    const hasHome = f => num(f.homeWorldspace) !== 0;
    function fmtDist(m) {
        m = Math.max(0, Math.round(num(m)));
        return m >= 1000 ? kmFormat[m >= 10000 ? 0 : 1].format(m / 1000) + ' km' : m + ' m';
    }

    // ── Roles and skills ─────────────────────────────────────────────────
    const SKILLS = [
        { key: 'oneHanded', name: 'One-handed', short: '1-Hand' },
        { key: 'twoHanded', name: 'Two-handed', short: '2-Hand' },
        { key: 'block', name: 'Block', short: 'Block' },
        { key: 'heavyArmor', name: 'Heavy armor', short: 'Heavy' },
        { key: 'lightArmor', name: 'Light armor', short: 'Light' },
        { key: 'archery', name: 'Archery', short: 'Archery' },
        { key: 'conjuration', name: 'Conjuration', short: 'Conj' },
        { key: 'restoration', name: 'Restoration', short: 'Resto' },
        { key: 'destruction', name: 'Destruction', short: 'Destr' }
    ];
    function roleOf(f) {
        if (f.isAnimal) return { name: 'Animal', mark: icon('paw-print'), note: 'Animal companion' };
        const s = f.skills || {};
        const melee = num(s.oneHanded) + num(s.twoHanded) + num(s.block);
        const magic = num(s.destruction) + num(s.restoration) + num(s.conjuration);
        const ranged = num(s.archery) * 2.5;
        const mage = magic >= melee && magic >= ranged, archer = !mage && ranged >= melee;
        if (mage && melee >= magic * 0.6) return { name: 'Spellsword', mark: glyph('oneHanded'), note: 'Blade and spell' };
        if (mage) return { name: 'Mage', mark: glyph('conjuration'), note: 'Fights with magic' };
        if (archer) return { name: 'Archer', mark: glyph('archery'), note: 'Fights at range' };
        return { name: 'Warrior', mark: glyph('twoHanded'), note: 'Fights up close' };
    }

    // ── Stage scaling ────────────────────────────────────────────────────
    // The stage fits the screen, then the player's interface scale applies.
    const SCALE_MIN = 70, SCALE_MAX = 170, SCALE_STEP = 5;
    function fit() {
        const w = window.innerWidth || STAGE_W, h = window.innerHeight || STAGE_H;
        const zoom = clamp(Math.min(w / STAGE_W, h / STAGE_H), 0.5, 3) * ui.scale;
        ui.zoom = zoom;
        const app = $('app'), width = w / zoom, height = h / zoom;
        app.style.zoom = zoom;
        app.style.width = width + 'px';
        app.style.height = height + 'px';
        // Above 100% the stage has less room than the 1600x900 design; the
        // layout condenses in steps (see "Condensed layouts" in horde.css).
        app.classList.toggle('compact', width < 1560);
        app.classList.toggle('narrow', width < 1200);
        app.classList.toggle('short', height < 760);
        app.classList.toggle('shorter', height < 600);
    }

    function sparks() {
        const box = $('sparks');
        let html = '';
        for (let i = 0; i < 26; i++) {
            const d = 6 + Math.random() * 7;
            html += '<i class="spark" style="left:' + (3 + Math.random() * 94).toFixed(1) + '%;--s:' + (1.6 + Math.random() * 2.6).toFixed(1) +
                'px;--d:' + d.toFixed(1) + 's;--delay:' + (-Math.random() * d).toFixed(1) + 's;--sway:' + ((Math.random() < .5 ? -1 : 1) * (10 + Math.random() * 34)).toFixed(0) + 'px"></i>';
        }
        box.innerHTML = html;
    }

    // ── Number tweens ────────────────────────────────────────────────────
    function tween(el, to, format) {
        to = Math.round(num(to));
        const from = el._value == null ? 0 : el._value;
        el._value = to;
        if (el._raf) cancelAnimationFrame(el._raf);
        if (from === to) { el.textContent = format ? format(to) : to; return; }
        const start = performance.now(), dur = 700;
        const step = now => {
            const t = Math.min(1, (now - start) / dur);
            const v = Math.round(from + (to - from) * easeOut(t));
            el.textContent = format ? format(v) : v;
            if (t < 1) el._raf = requestAnimationFrame(step);
        };
        el._raf = requestAnimationFrame(step);
    }

    // ── Header ───────────────────────────────────────────────────────────
    function renderHeader() {
        const lit = model.followers.filter(isLit).length;
        const ash = model.followers.length - lit;
        const max = clamp(Math.round(num(model.maxFollowers, 20)), 1, 20);
        tween($('cap-count'), model.followers.length);
        $('cap-max').textContent = '/ ' + max;
        $('count-followers').textContent = model.followers.length;
        $('count-dismissed').textContent = model.dismissed.length || num(model.dismissedCount);
        const pips = $('pips');
        const slots = Math.max(max, model.followers.length);
        while (pips.children.length < slots) pips.appendChild(document.createElement('i'));
        while (pips.children.length > slots) pips.lastChild.remove();
        Array.from(pips.children).forEach((pip, i) => {
            pip.className = 'pip' + (i < lit ? ' lit' : i < lit + ash ? ' ash' : '');
            pip.style.setProperty('--i', i);
        });
        $('legend-lit').textContent = lit;
        $('legend-ash').textContent = ash;
    }

    // ── Muster line ──────────────────────────────────────────────────────
    const TICKS = [[5, '5 m'], [10, '10'], [25, '25'], [50, '50'], [100, '100'], [250, '250'], [500, '500'], [1000, '1 km'], [2000, '2 km+']];
    const xOf = meters => clamp(Math.log1p(Math.max(0, num(meters))) / Math.log1p(MUSTER_MAX_METERS), 0, 1);
    const blips = new Map();

    function buildRail() {
        const rail = $('rail');
        rail.insertAdjacentHTML('beforeend', TICKS.map(([m, label]) =>
            '<div class="tick" style="left:' + (xOf(m) * 100).toFixed(2) + '%"><span>' + label + '</span></div>').join(''));
    }

    function renderMuster() {
        const rail = $('rail');
        const followers = model.followers;
        const seen = new Set();
        const arriving = ui.arrive;
        ui.arrive = false;
        // Followers at nearly the same distance stack into lanes.
        const lanes = [0, -15, 15, -30, 30];
        const laneEnds = lanes.map(() => -1);
        const ordered = followers.map(f => ({ f, x: xOf(f.distance) })).sort((a, b) => a.x - b.x);
        ordered.forEach(({ f, x }, i) => {
            let lane = laneEnds.findIndex(end => x - end > 0.016);
            if (lane < 0) lane = 0;
            laneEnds[lane] = x;
            let blip = blips.get(f.formID);
            if (!blip) {
                blip = document.createElement('button');
                blip.className = 'blip arriving';
                blip.tabIndex = -1;
                blip.innerHTML = '<span class="blip-core"></span><span class="blip-flag"></span>';
                blip.style.left = '0%';
                blip.style.setProperty('--flick', (1.7 + Math.random() * 1.4).toFixed(2) + 's');
                blip.addEventListener('click', () => select(f.formID));
                rail.appendChild(blip);
                blips.set(f.formID, blip);
            } else if (arriving) {
                blip.classList.add('arriving');
                blip.style.left = '0%';
            }
            seen.add(f.formID);
            blip.classList.toggle('lit', isLit(f));
            blip.classList.toggle('ash', !isLit(f));
            blip.classList.toggle('selected', f.formID === ui.selected);
            blip.lastChild.innerHTML = esc(f.name || 'Unknown') + '<i>' + fmtDist(f.distance) + '</i>';
            blip.setAttribute('aria-label', (f.name || 'Unknown') + ', ' + fmtDist(f.distance));
            blip.style.top = (35 + lanes[lane]) + 'px';
            blip._x = x;
            blip.style.transitionDelay = blip.classList.contains('arriving') ? (i * 70) + 'ms' : '0ms';
        });
        for (const [id, blip] of blips) {
            if (!seen.has(id)) { blip.remove(); blips.delete(id); }
        }
        // Let arriving blips paint at YOU, then glide out to their distance.
        requestAnimationFrame(() => requestAnimationFrame(() => {
            for (const blip of blips.values()) {
                blip.classList.remove('arriving');
                blip.style.left = (blip._x * 100).toFixed(3) + '%';
            }
        }));

        const note = $('muster-note');
        if (!followers.length) {
            note.textContent = model.loaded ? 'Followers appear on this line by distance.' : '';
        } else {
            const far = followers.reduce((a, b) => (num(b.distance) > num(a.distance) ? b : a));
            note.innerHTML = 'Farthest: <b>' + esc(far.name) + '</b>, ' + fmtDist(far.distance) +
                (far.location ? ' away in ' + esc(far.location) : ' away');
        }
    }

    // ── Roster ───────────────────────────────────────────────────────────
    const rows = new Map();
    function sortedFollowers() {
        const list = model.followers.slice();
        if (ui.sort === 'nearest') list.sort((a, b) => num(a.distance) - num(b.distance));
        else if (ui.sort === 'level') list.sort((a, b) => num(b.level) - num(a.level));
        return list;
    }

    function createRow(f) {
        const row = document.createElement('button');
        row.className = 'row';
        row.innerHTML = '<span class="role"></span><span class="row-text"><span class="row-name"></span><span class="row-sub"></span></span>' +
            '<span class="minibars"><span class="minibar h"><i></i></span><span class="minibar m"><i></i></span><span class="minibar s"><i></i></span></span>' +
            '<span class="row-dist"></span>';
        row.addEventListener('click', () => select(f.formID));
        row.addEventListener('focus', () => select(f.formID));
        row.addEventListener('animationend', e => {
            if (e.target !== row) return;
            if (e.animationName === 'hd-fade-up') row.classList.remove('enter');
            if (e.animationName === 'hd-sheen') row.classList.remove('just-selected');
        });
        row.addEventListener('keydown', e => {
            if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
            const next = e.key === 'ArrowDown' ? row.nextElementSibling : row.previousElementSibling;
            if (next && next.classList.contains('row')) { next.focus(); e.preventDefault(); }
        });
        return row;
    }

    function updateRow(row, f) {
        const role = roleOf(f);
        const [roleEl, text, bars, dist] = row.children;
        if (roleEl._role !== role.name) { roleEl.innerHTML = role.mark; roleEl._role = role.name; }
        row.classList.toggle('lit', isLit(f));
        row.classList.toggle('ash', !isLit(f));
        row.classList.toggle('selected', f.formID === ui.selected);
        text.firstChild.textContent = f.name || 'Unknown';
        const flags = [];
        if (hasHome(f)) flags.push(icon('house'));
        if (f.isPassive) flags.push(icon('shield-off'));
        if (f.isSandboxEnabled) flags.push(icon('tent'));
        if (f.isFollowClose) flags.push(icon('magnet'));
        if (f.isEssential === false) flags.push(icon('skull'));
        text.lastChild.innerHTML = '<span>Lv ' + Math.round(num(f.level, 1)) + ' · ' + esc(f.className || role.name) + '</span>' +
            (flags.length ? '<span class="flags">' + flags.join('') + '</span>' : '');
        const values = [ratio(f.health, f.healthMax), ratio(f.magicka, f.magickaMax), ratio(f.stamina, f.staminaMax)];
        Array.from(bars.children).forEach((bar, i) => bar.firstChild.style.setProperty('--v', values[i].toFixed(3)));
        dist.innerHTML = fmtDist(f.distance) + '<small>' + (isLit(f) ? 'Following' : 'Waiting') + '</small>';
        row.setAttribute('aria-label', (f.name || 'Unknown') + ', level ' + Math.round(num(f.level, 1)) + ', ' + (isLit(f) ? 'following' : 'waiting'));
    }

    function renderRoster() {
        const list = $('roster');
        if (!model.loaded) {
            rows.clear();
            list.innerHTML = Array.from({ length: 6 }, () =>
                '<div class="skeleton"><i style="width:30px;height:30px;border-radius:50%"></i><span style="flex:1;display:grid;gap:8px"><i style="width:60%"></i><i style="width:35%;height:8px"></i></span><i style="width:44px"></i></div>').join('');
            return;
        }
        list.querySelectorAll('.skeleton, .empty-line').forEach(el => el.remove());
        const followers = sortedFollowers();
        if (!followers.length) {
            rows.forEach(row => row.remove());
            rows.clear();
            list.innerHTML = '<p class="empty-line" style="padding:22px 22px 0">No one is following you right now.</p>';
            return;
        }
        const seen = new Set();
        const live = () => Array.from(list.children).filter(el => !el.classList.contains('leave'));
        followers.forEach((f, i) => {
            let row = rows.get(f.formID);
            if (!row) {
                row = createRow(f);
                row.classList.add('enter');
                row.style.setProperty('--i', i);
                rows.set(f.formID, row);
            }
            updateRow(row, f);
            seen.add(f.formID);
            const at = live()[i];
            if (at !== row) list.insertBefore(row, at || null);
        });
        for (const [id, row] of rows) {
            if (seen.has(id)) continue;
            rows.delete(id);
            list.appendChild(row);
            row.classList.add('leave');
            setTimeout(() => row.remove(), 260);
        }
    }

    // ── Dossier ──────────────────────────────────────────────────────────
    const RING_C = 2 * Math.PI * 42;
    const VITALS = [
        { cls: 'h', key: 'health', max: 'healthMax', name: 'Health', glyph: 'health' },
        { cls: 'm', key: 'magicka', max: 'magickaMax', name: 'Magicka', glyph: 'magicka' },
        { cls: 's', key: 'stamina', max: 'staminaMax', name: 'Stamina', glyph: 'stamina' }
    ];
    const BEHAVIORS = [
        { id: 'passive', key: 'isPassive', action: 'hordeSetPassive', field: 'passive', fallback: false, icon: 'shield-off', label: 'Passive',
          on: 'Won’t start fights', off: 'Fights enemies', toastOn: n => n + ' won’t start fights.', toastOff: n => n + ' will fight enemies.' },
        { id: 'close', key: 'isFollowClose', action: 'hordeSetFollowClose', field: 'enabled', fallback: false, icon: 'magnet', label: 'Follow close',
          on: 'Teleports to you when left behind', off: 'Never teleports to you', toastOn: n => n + ' will teleport to you when left behind.', toastOff: n => n + ' won’t teleport to you.' },
        { id: 'sandbox', key: 'isSandboxEnabled', action: 'hordeSetSandbox', field: 'enabled', fallback: false, icon: 'tent', label: 'Idle in safe places',
          on: 'Wanders in safe places when idle', off: 'Doesn’t wander when idle', toastOn: n => n + ' will wander in safe places when idle.', toastOff: n => n + ' won’t wander when idle.' },
        { id: 'essential', key: 'isEssential', action: 'hordeSetEssential', field: 'essential', fallback: true, icon: 'shield-check', label: 'Essential',
          on: 'Can’t be killed', off: 'Can be killed', toastOn: n => n + ' can’t be killed.', toastOff: n => n + ' can be killed.' }
    ];
    const GEAR = [
        { key: 'rightHand', name: 'Right', glyph: 'sword' }, { key: 'leftHand', name: 'Left', glyph: 'shield' },
        { key: 'head', name: 'Head', glyph: 'helmet' }, { key: 'body', name: 'Body', glyph: 'chestplate' },
        { key: 'hands', name: 'Hands', glyph: 'gauntlet' }, { key: 'feet', name: 'Feet', glyph: 'boot' }
    ];
    let dossierBuilt = false;
    let shownId = 0;
    let radarFrom = SKILLS.map(() => 0);

    function buildDossier() {
        const d = $('dossier');
        d.innerHTML =
            '<div class="dossier-head">' +
              '<div class="dossier-id"><h1 class="dossier-name"><span id="d-name"></span><i></i></h1><div class="dossier-meta" id="d-meta"></div></div>' +
              '<div class="seg lg" role="group" aria-label="Order">' +
                '<button id="d-follow">' + icon('footprints') + 'Follow</button>' +
                '<button id="d-wait">' + icon('hourglass') + 'Wait</button>' +
              '</div>' +
              '<div class="actions">' +
                '<button class="btn primary lg" id="d-summon">' + icon('locate-fixed') + 'Summon</button>' +
                '<button class="btn danger lg" id="d-dismiss">' + icon('door-open') + 'Dismiss</button>' +
              '</div>' +
            '</div>' +
            '<div class="dossier-body" id="d-body">' +
              '<section class="panel"><div class="panel-head"><span class="panel-title">Vitals</span></div><div class="panel-body">' +
                '<div class="rings">' + VITALS.map(v =>
                    '<div class="ring ' + v.cls + '" id="ring-' + v.cls + '"><svg viewBox="0 0 100 100">' +
                      '<circle class="track" cx="50" cy="50" r="42"/>' +
                      '<circle class="glow" cx="50" cy="50" r="42" stroke-dasharray="' + RING_C.toFixed(2) + '" stroke-dashoffset="' + RING_C.toFixed(2) + '"/>' +
                      '<circle class="value" cx="50" cy="50" r="42" stroke-dasharray="' + RING_C.toFixed(2) + '" stroke-dashoffset="' + RING_C.toFixed(2) + '"/>' +
                      '<g class="tip"><circle class="tip-glow" cx="92" cy="50" r="7"/><circle class="tip-core" cx="92" cy="50" r="3"/></g></svg>' +
                      '<div class="ring-center"><b></b><small></small></div>' +
                      '<div class="ring-label">' + glyph(v.glyph) + '<span class="label">' + v.name + '</span></div></div>').join('') +
                '</div>' +
                '<div class="stat-strip"><div class="stat">' + glyph('armor') + '<div><b id="d-armor">0</b><span class="micro">Armor rating</span></div></div>' +
                '<div class="stat">' + icon('flame') + '<div><b id="d-level">0</b><span class="micro">Level</span></div></div></div>' +
              '</div></section>' +
              '<section class="panel"><div class="panel-head"><span class="panel-title">Combat profile</span></div><div class="panel-body" id="d-profile"></div></section>' +
              '<section class="panel"><div class="panel-head"><span class="panel-title">Behavior</span></div><div class="panel-body behaviors" style="padding-top:4px;padding-bottom:4px">' +
                BEHAVIORS.map(b => '<button class="behavior" role="switch" id="beh-' + b.id + '">' + icon(b.icon) +
                    '<span class="behavior-text"><b>' + b.label + '</b><small></small></span><span class="switch"></span></button>').join('') +
              '</div></section>' +
              '<section class="panel"><div class="panel-head"><span class="panel-title">Equipment</span></div><div class="panel-body" style="padding-top:6px;padding-bottom:6px" id="d-gear"></div></section>' +
              '<section class="panel"><div class="panel-head"><span class="panel-title">Perks</span><span class="count-badge aside" id="d-perk-count">0</span></div><div class="panel-body"><div class="chips" id="d-perks"></div></div></section>' +
              '<section class="panel"><div class="panel-head"><span class="panel-title">Home</span></div><div class="panel-body" id="d-home"></div></section>' +
            '</div>';
        d.querySelector('.stat-strip svg.i').classList.add('g');

        $('d-follow').addEventListener('click', () => withSelected(f => {
            if (isLit(f)) return;
            order('hordeSetFollow', { formID: f.formID }, () => toastFor(f.formID, n => n + ' is following you.', 'footprints'),
                () => { const now = followerById(f.formID); return now && isLit(now); });
        }));
        $('d-wait').addEventListener('click', () => withSelected(f => {
            if (!isLit(f)) return;
            order('hordeSetWait', { formID: f.formID }, () => toastFor(f.formID, n => n + ' is waiting here.', 'hourglass'),
                () => { const now = followerById(f.formID); return now && !isLit(now); });
        }));
        $('d-summon').addEventListener('click', () => withSelected(f =>
            order('hordeSummon', { formID: f.formID }, () => toastFor(f.formID, n => 'Summoned ' + n + '.', 'locate-fixed'))));
        $('d-dismiss').addEventListener('click', () => withSelected(f => confirmDialog({
            title: 'Dismiss ' + (f.name || 'follower') + '?',
            body: '<b>' + esc(f.name) + '</b> leaves your horde and goes ' + (hasHome(f) ? 'to <b>' + esc(f.homeName) + '</b>.' : 'back to where they were recruited.') +
                ' You can summon them again from Dismissed.',
            confirm: 'Dismiss', confirmIcon: 'door-open',
            onConfirm: () => {
                const name = f.name;
                order('hordeDismiss', { formID: f.formID }, () => toast('Dismissed <b>' + esc(name) + '</b>.', 'door-open'));
            }
        })));
        BEHAVIORS.forEach(b => $('beh-' + b.id).addEventListener('click', () => withSelected(f => {
            const value = typeof f[b.key] === 'boolean' ? f[b.key] : b.fallback;
            order(b.action, { formID: f.formID, [b.field]: !value },
                () => toastFor(f.formID, n => (!value ? b.toastOn(n) : b.toastOff(n)), b.icon),
                () => { const now = followerById(f.formID); return now && now[b.key] === !value; });
        })));
        dossierBuilt = true;
    }

    // Arc and tip move together; the tip hides on an empty ring.
    function sweepRing(ring, to) {
        const from = ring._ratio == null ? 0 : ring._ratio;
        ring._ratio = to;
        const value = ring.querySelector('.value'), glow = ring.querySelector('.glow'), tip = ring.querySelector('.tip');
        if (ring._raf) cancelAnimationFrame(ring._raf);
        const start = performance.now(), dur = 950;
        const step = now => {
            const t = Math.min(1, (now - start) / dur);
            const r = from + (to - from) * easeOut(t);
            const offset = (RING_C * (1 - r)).toFixed(2);
            value.setAttribute('stroke-dashoffset', offset);
            glow.setAttribute('stroke-dashoffset', offset);
            tip.setAttribute('transform', 'rotate(' + (r * 360).toFixed(2) + ' 50 50)');
            tip.style.opacity = r > 0.005 ? 1 : 0;
            if (t < 1) ring._raf = requestAnimationFrame(step);
        };
        ring._raf = requestAnimationFrame(step);
    }

    function withSelected(fn) {
        const f = followerById(ui.selected);
        if (f) fn(f);
    }
    // text() receives the follower's name, escaped and emphasized.
    function toastFor(id, text, iconName) {
        const f = followerById(id) || dismissedById(id);
        if (f) toast(text('<b>' + esc(f.name || 'Unknown') + '</b>'), iconName);
    }

    function renderDossier(selectionChanged) {
        const d = $('dossier');
        if (!model.loaded) return;
        const f = followerById(ui.selected);
        if (!f) {
            dossierBuilt = false;
            shownId = 0;
            d.className = 'dossier';
            d.innerHTML = '<div class="empty"><div class="empty-mark">' + icon('flame') + '</div><h2>No followers yet</h2>' +
                '<p>Ask someone to follow you and they join your horde. Their vitals, gear and orders appear here.</p></div>';
            return;
        }
        if (!dossierBuilt) buildDossier();
        const swapped = shownId !== f.formID;
        shownId = f.formID;
        d.classList.toggle('ash', !isLit(f));
        if (swapped || selectionChanged) {
            d.classList.remove('swap');
            void d.offsetWidth;
            d.classList.add('swap');
        }

        $('d-name').textContent = f.name || 'Unknown';
        const role = roleOf(f);
        const meta = [
            '<span class="status-pill ' + (isLit(f) ? 'lit' : 'ash') + '"><i class="dot ' + (isLit(f) ? 'lit' : 'ash') + '"></i>' + (isLit(f) ? 'Following' : 'Waiting') + '</span>',
            '<span>' + esc(f.race || 'Unknown') + '</span>',
            '<span>' + esc(f.className || role.name) + '</span>',
            '<span>' + icon('map-pin') + esc(f.location || 'Unknown') + ' · ' + fmtDist(f.distance) + ' away</span>'
        ];
        $('d-meta').innerHTML = meta.join('<i class="sep"></i>');
        $('d-follow').setAttribute('aria-pressed', String(isLit(f)));
        $('d-wait').setAttribute('aria-pressed', String(!isLit(f)));

        VITALS.forEach(v => {
            const ring = $('ring-' + v.cls);
            const r = ratio(f[v.key], f[v.max]);
            sweepRing(ring, r);
            ring.classList.toggle('low', v.cls === 'h' && r > 0 && r < 0.3);
            tween(ring.querySelector('b'), f[v.key]);
            ring.querySelector('small').textContent = 'of ' + Math.round(num(f[v.max]));
        });
        tween($('d-armor'), f.armorRating);
        tween($('d-level'), f.level);

        renderProfile(f, role, swapped);

        BEHAVIORS.forEach(b => {
            const el = $('beh-' + b.id);
            const value = typeof f[b.key] === 'boolean' ? f[b.key] : b.fallback;
            el.setAttribute('aria-checked', String(value));
            el.querySelector('small').textContent = value ? b.on : b.off;
        });

        const equipment = f.equipment || {};
        $('d-gear').innerHTML = f.isAnimal ? '<p class="empty-line">Animals don’t wear gear.</p>' : GEAR.map(g => {
            const item = equipment[g.key] || {};
            const name = item.name && item.name !== 'None' ? item.name : '';
            const stat = Math.round(num(item.stat));
            const statText = !name || stat <= 0 ? '' : item.isWeapon ? stat + ' dmg' : stat + ' AR';
            return '<div class="gear-row' + (name ? '' : ' none') + '"><span class="gear-icon">' + glyph(g.glyph) + '</span>' +
                '<span class="micro">' + g.name + '</span><b>' + (name ? esc(name) : 'Nothing') + '</b>' +
                (statText ? '<span class="gear-stat">' + statText + '</span>' : '<span></span>') + '</div>';
        }).join('');

        const perks = Array.isArray(f.perks) ? f.perks.filter((p, i, all) => p && all.indexOf(p) === i) : [];
        $('d-perk-count').textContent = perks.length;
        const perkKey = perks.join('\u0001');
        const perkBox = $('d-perks');
        if (perkBox._key !== perkKey || swapped) {
            perkBox._key = perkKey;
            perkBox.innerHTML = perks.length
                ? perks.map((p, i) => '<span class="chip" style="--i:' + Math.min(i, 30) + '">' + esc(p) + '</span>').join('')
                : '<p class="empty-line">No perks.</p>';
        }

        const home = hasHome(f);
        const homeBox = $('d-home');
        const homeKey = f.formID + '|' + home + '|' + (f.homeName || '');
        if (homeBox._key === homeKey) return;
        homeBox._key = homeKey;
        homeBox.innerHTML =
            '<div class="home' + (home ? ' has-home' : '') + '"><span class="home-mark">' + icon('house') + '</span>' +
            '<span class="home-text"><span class="micro">Goes here when dismissed</span><b>' + (home ? esc(f.homeName || 'Unknown') : 'No home set') + '</b>' +
            '<small>' + (home ? 'Set home here to move it to your location.' : 'Set home here to use your current location.') + '</small></span></div>' +
            '<div class="home-actions" style="margin:14px 0 0">' +
              '<button class="btn tinted" id="d-sethome">' + icon('house-plus') + (home ? 'Move home here' : 'Set home here') + '</button>' +
              (home ? '<button class="btn ghost" id="d-clearhome">' + icon('x') + 'Clear home</button>' : '') +
            '</div>';
        $('d-sethome').addEventListener('click', () => order('hordeSetHome', { formID: f.formID },
            () => toastFor(f.formID, n => n + ' will go to <b>' + esc((followerById(f.formID) || {}).homeName || 'this place') + '</b> when dismissed.', 'house')));
        if (home) $('d-clearhome').addEventListener('click', () => confirmDialog({
            title: 'Clear home?',
            body: '<b>' + esc(f.name) + '</b> will go back to where they were recruited when dismissed.',
            confirm: 'Clear home', confirmIcon: 'x',
            onConfirm: () => order('hordeClearHome', { formID: f.formID }, () => toastFor(f.formID, n => 'Cleared ' + n + '’s home.', 'house'))
        }));
    }

    // Combat profile: a nine-skill radar that morphs between followers.
    const RADAR = { size: 172, r: 56, labelR: 74 };
    function radarPoint(i, value) {
        const angle = -Math.PI / 2 + i * 2 * Math.PI / SKILLS.length;
        const r = RADAR.r * value;
        return [RADAR.size / 2 + Math.cos(angle) * r, RADAR.size / 2 + Math.sin(angle) * r];
    }
    const polygon = values => values.map((v, i) => radarPoint(i, v).map(n => n.toFixed(1)).join(',')).join(' ');

    function renderProfile(f, role, swapped) {
        const box = $('d-profile');
        if (f.isAnimal) {
            box._animal = true;
            box.innerHTML = '<div class="role-badge"><span class="role">' + role.mark + '</span><div><b>Animal</b><span class="micro">' + role.note + '</span></div></div>' +
                '<p class="animal-note" style="margin:14px 0 0">Animals fight with tooth and claw. They don’t train skills or wear gear.</p>';
            radarFrom = SKILLS.map(() => 0);
            return;
        }
        if (!box.querySelector('.radar') || box._animal) {
            box._animal = false;
            const s = RADAR.size;
            const rings = [0.25, 0.5, 0.75, 1].map(k => '<polygon class="grid' + (k === 1 ? ' outer' : '') + '" points="' + polygon(SKILLS.map(() => k)) + '"/>').join('');
            const spokes = SKILLS.map((_, i) => { const [x, y] = radarPoint(i, 1); return '<line class="spoke" x1="' + s / 2 + '" y1="' + s / 2 + '" x2="' + x.toFixed(1) + '" y2="' + y.toFixed(1) + '"/>'; }).join('');
            const labels = SKILLS.map((sk, i) => {
                const angle = -Math.PI / 2 + i * 2 * Math.PI / SKILLS.length;
                return '<span class="radar-label" data-skill="' + sk.key + '" style="left:' + (s / 2 + Math.cos(angle) * RADAR.labelR).toFixed(1) + 'px;top:' + (s / 2 + Math.sin(angle) * RADAR.labelR).toFixed(1) + 'px">' + sk.short + '</span>';
            }).join('');
            box.innerHTML = '<div class="profile"><div class="radar"><svg viewBox="0 0 ' + s + ' ' + s + '">' + rings + spokes +
                '<polygon class="area-glow"/><polygon class="area"/>' + SKILLS.map(() => '<circle class="vertex" r="2.6"/>').join('') + '</svg>' + labels + '</div>' +
                '<div><div class="role-badge"><span class="role"></span><div><b></b><span class="micro"></span></div></div><div class="top-skills"></div></div></div>';
        }
        const badge = box.querySelector('.role-badge');
        badge.querySelector('.role').innerHTML = role.mark;
        badge.querySelector('b').textContent = role.name;
        badge.querySelector('.micro').textContent = role.note;

        const skills = f.skills || {};
        const values = SKILLS.map(sk => clamp(num(skills[sk.key]) / 100, 0.04, 1));
        const ranked = SKILLS.map(sk => ({ sk, v: num(skills[sk.key]) })).sort((a, b) => b.v - a.v).slice(0, 3);
        box.querySelectorAll('.radar-label').forEach(l => l.classList.toggle('top', ranked.some(r => r.sk.key === l.dataset.skill)));
        box.querySelector('.top-skills').innerHTML = ranked.map(({ sk, v }) =>
            '<div class="skill-row">' + glyph(sk.key) + '<span>' + sk.name + '</span><b>' + Math.round(v) + '</b>' +
            '<span class="skill-bar"><i style="--v:' + clamp(v / 100, 0, 1).toFixed(3) + '"></i></span></div>').join('');

        const svg = box.querySelector('svg');
        const area = svg.querySelector('.area'), glow = svg.querySelector('.area-glow'), dots = svg.querySelectorAll('.vertex');
        const from = radarFrom.slice(), start = performance.now(), dur = swapped ? 650 : 400;
        radarFrom = values;
        if (box._raf) cancelAnimationFrame(box._raf);
        const step = now => {
            const t = easeOut(Math.min(1, (now - start) / dur));
            const current = values.map((v, i) => from[i] + (v - from[i]) * t);
            const pts = polygon(current);
            area.setAttribute('points', pts);
            glow.setAttribute('points', pts);
            current.forEach((v, i) => { const [x, y] = radarPoint(i, v); dots[i].setAttribute('cx', x.toFixed(1)); dots[i].setAttribute('cy', y.toFixed(1)); });
            if (t < 1) box._raf = requestAnimationFrame(step);
        };
        box._raf = requestAnimationFrame(step);
    }

    function select(id) {
        id = Number(id);
        if (!id || ui.selected === id) return;
        ui.selected = id;
        rows.forEach((row, rowId) => {
            row.classList.toggle('selected', rowId === id);
            row.classList.remove('just-selected');
        });
        const picked = rows.get(id);
        if (picked) {
            void picked.offsetWidth;
            picked.classList.add('just-selected');
        }
        blips.forEach((blip, blipId) => blip.classList.toggle('selected', blipId === id));
        renderDossier(true);
    }

    // ── Dismissed ────────────────────────────────────────────────────────
    function renderDismissed() {
        const page = $('dismissed');
        // Native sends the registry in hash order.
        const list = model.dismissed.slice().sort((a, b) => collator.compare(a.name || '', b.name || '') || a.formID - b.formID);
        const head = '<div class="page-head"><div><span class="micro">Registry</span><h2>Dismissed followers</h2></div>' +
            '<p>Followers you’ve let go. Summon them to you, or change where they go home to.</p></div>';
        if (!list.length) {
            page.innerHTML = head + '<section class="panel" style="height:360px"><div class="empty"><div class="empty-mark">' + icon('user-x') + '</div>' +
                '<h2>No one dismissed</h2><p>When you dismiss a follower, they’re listed here so you can find them again.</p></div></section>';
            return;
        }
        page.innerHTML = head + '<section class="panel"><table class="table"><thead><tr><th>Follower</th><th>Home</th><th style="text-align:right">Actions</th></tr></thead><tbody>' +
            list.map((d, i) => {
                const id = d.formID;
                return '<tr style="--i:' + Math.min(i, 20) + '"><td><div class="cell-name"><span class="role">' + icon('user-x') + '</span><b>' + esc(d.name || 'Unknown') + '</b></div></td>' +
                    '<td><span class="cell-home' + (d.hasHome ? '' : ' none') + '">' + icon('house') + (d.hasHome ? esc(d.homeName) : 'Where they were recruited') + '</span></td>' +
                    '<td><div class="cell-actions">' +
                      '<button class="btn tinted sm" data-act="summon" data-id="' + id + '">' + icon('locate-fixed') + 'Summon</button>' +
                      '<button class="btn sm" data-act="home" data-id="' + id + '">' + icon('house-plus') + 'Set home here</button>' +
                      '<button class="btn ghost sm" data-act="clear" data-id="' + id + '"' + (d.hasHome ? '' : ' disabled') + '>' + icon('x') + 'Clear home</button>' +
                      '<button class="btn danger sm" data-act="forget" data-id="' + id + '">' + icon('trash-2') + 'Forget</button>' +
                    '</div></td></tr>';
            }).join('') + '</tbody></table></section>' +
            '<p class="page-note">' + icon('info') + 'Summon brings a dismissed follower to you without adding them back to your horde.</p>';
    }

    function onDismissedClick(e) {
        const button = e.target.closest('button[data-act]');
        if (!button) return;
        const id = Number(button.getAttribute('data-id'));
        const d = dismissedById(id);
        if (!d) return;
        const name = '<b>' + esc(d.name || 'Unknown') + '</b>';
        switch (button.getAttribute('data-act')) {
        case 'summon': order('hordeSummonDismissed', { formID: id }, () => toast('Summoned ' + name + '.', 'locate-fixed')); break;
        case 'home': order('hordeSetDismissedHome', { formID: id }, () => {
            const now = dismissedById(id);
            toast(name + ' now lives at <b>' + esc(now && now.homeName ? now.homeName : 'your location') + '</b>.', 'house');
        }); break;
        case 'clear': order('hordeClearDismissedHome', { formID: id }, () => toast('Cleared ' + name + '’s home.', 'house')); break;
        case 'forget': confirmDialog({
            title: 'Forget ' + (d.name || 'follower') + '?',
            body: name + ' is removed from the registry, along with their home. You can recruit them again in the world.',
            confirm: 'Forget', confirmIcon: 'trash-2',
            onConfirm: () => order('hordeForgetFollower', { formID: id }, () => toast('Forgot ' + name + '.', 'trash-2'))
        }); break;
        }
    }

    // ── Settings ─────────────────────────────────────────────────────────
    const DISTANCES = [['close', 'Close'], ['normal', 'Normal'], ['far', 'Far']];
    const SPACING = { close: [70, 112, 154], normal: [110, 200, 290], far: [170, 340, 510] };
    let settingsBuilt = false;

    function buildSettings() {
        $('settings').innerHTML =
            '<div class="page-head"><div><span class="micro">Preferences</span><h2>Settings</h2></div><p>Changes apply right away.</p></div>' +
            '<div class="settings-grid">' +
              '<section class="panel setting setting-wide"><div><h3>Interface scale</h3><p>Make Horde larger or smaller on your screen. 100% fits Horde to your screen.</p></div>' +
                '<div class="limit scale"><b id="set-scale-value">100%</b><input class="range" id="set-scale" type="range" min="' + SCALE_MIN + '" max="' + SCALE_MAX + '" step="' + SCALE_STEP + '" aria-label="Interface scale">' +
                '<button class="btn ghost" id="set-scale-reset">Default</button></div></section>' +
              '<section class="panel setting setting-wide"><div><h3>Follow distance</h3><p>How far behind you followers walk.</p></div>' +
                '<div class="seg lg" id="set-distance" role="group" aria-label="Follow distance">' +
                  DISTANCES.map(([v, l]) => '<button data-value="' + v + '">' + l + '</button>').join('') + '</div>' +
                '<div class="spacing"><span class="you-mark"></span>' + [0, 1, 2].map(() => '<i class="follower-dot"></i>').join('') + '<small>Preview</small></div></section>' +
              '<section class="panel setting"><div><h3>Follower limit</h3><p>How many followers can be in your horde at once.</p></div>' +
                '<div class="limit"><b id="set-limit-value">20</b><input class="range" id="set-limit" type="range" min="1" max="20" step="1" aria-label="Follower limit"></div>' +
                '<div class="limit-pips" id="set-limit-pips"></div></section>' +
              '<section class="panel setting"><div><h3>Order shortcuts</h3><p>Give orders with Horde’s keybinds, or from the favorites menu.</p></div>' +
                '<div class="seg lg" id="set-input" role="group" aria-label="Order shortcuts">' +
                  '<button data-value="keys">' + icon('keyboard') + 'Keybinds</button>' +
                  '<button data-value="favorites">' + icon('star') + 'Favorites</button></div></section>' +
              '<section class="panel setting"><div><h3>New followers wander</h3><p>Followers you recruit from now on start with Idle in safe places turned on.</p></div>' +
                '<button class="switch-btn" role="switch" id="set-sandbox"><span></span><i class="switch"></i></button></section>' +
              '<section class="panel setting"><div><h3>Notifications</h3><p>Show Horde’s messages in the corner of the screen.</p></div>' +
                '<button class="switch-btn" role="switch" id="set-notify"><span></span><i class="switch"></i></button></section>' +
            '</div>';
        // Previewed while dragging, applied on release: rescaling mid-drag
        // would move the slider out from under the cursor.
        const scale = $('set-scale');
        scale.addEventListener('input', () => previewScale(Number(scale.value)));
        scale.addEventListener('change', () => setScale(Number(scale.value)));
        $('set-scale-reset').addEventListener('click', () => setScale(100));
        $('set-distance').addEventListener('click', e => {
            const b = e.target.closest('button[data-value]');
            if (!b || b.getAttribute('data-value') === model.followDistance) return;
            const label = b.textContent;
            order('hordeUpdateSettings', { followDistance: b.getAttribute('data-value') }, () => toast('Follow distance set to <b>' + label + '</b>.', 'footprints'));
        });
        const limit = $('set-limit');
        limit.addEventListener('input', () => previewLimit(Number(limit.value)));
        limit.addEventListener('change', () => {
            const value = Number(limit.value);
            order('hordeUpdateSettings', { maxFollowers: value }, () => toast('Follower limit set to <b>' + value + '</b>.', 'users'));
        });
        $('set-input').addEventListener('click', e => {
            const b = e.target.closest('button[data-value]');
            if (!b || (b.getAttribute('data-value') === 'keys') === !!model.useKeybinds) return;
            order('hordeToggleInputMode', {}, () => toast(model.useKeybinds ? 'Orders now use <b>keybinds</b>.' : 'Orders now use the <b>favorites menu</b>.', model.useKeybinds ? 'keyboard' : 'star'));
        });
        $('set-sandbox').addEventListener('click', () => {
            const next = !model.defaultSandboxEnabled;
            order('hordeUpdateSettings', { defaultSandboxEnabled: next }, () => toast(next ? 'New followers will wander in safe places.' : 'New followers won’t wander.', 'tent'));
        });
        $('set-notify').addEventListener('click', () => {
            const next = !model.notificationsEnabled;
            order('hordeUpdateSettings', { notificationsEnabled: next }, () => toast(next ? 'Horde messages are on.' : 'Horde messages are off.', next ? 'bell' : 'bell-off'));
        });
        settingsBuilt = true;
    }

    function previewScale(value) {
        const scale = $('set-scale');
        $('set-scale-value').textContent = value + '%';
        scale.style.setProperty('--fill', ((value - SCALE_MIN) / (SCALE_MAX - SCALE_MIN) * 100).toFixed(1) + '%');
        $('set-scale-reset').disabled = value === 100;
    }

    function applyScale(percent) {
        const value = clamp(Math.round(num(percent, 100)), SCALE_MIN, SCALE_MAX);
        if (value / 100 === ui.scale) return;
        ui.scale = value / 100;
        fit();
    }

    function setScale(value) {
        value = clamp(Math.round(value / SCALE_STEP) * SCALE_STEP, SCALE_MIN, SCALE_MAX);
        $('set-scale').value = value;
        previewScale(value);
        applyScale(value);
        if (value === num(model.uiScale, 100)) return;
        order('hordeUpdateSettings', { uiScale: value }, () => toast('Interface scale set to <b>' + value + '%</b>.', 'settings-2'));
    }

    function previewLimit(value) {
        const limit = $('set-limit');
        $('set-limit-value').textContent = value;
        limit.style.setProperty('--fill', ((value - 1) / 19 * 100).toFixed(1) + '%');
        const pips = $('set-limit-pips');
        if (pips.children.length !== 20) pips.innerHTML = Array.from({ length: 20 }, () => '<i class="pip"></i>').join('');
        Array.from(pips.children).forEach((pip, i) => { pip.className = 'pip' + (i < model.followers.length ? ' lit' : i < value ? ' ash' : ''); });
    }

    function renderSettings() {
        if (!settingsBuilt) buildSettings();
        const scale = $('set-scale');
        if (document.activeElement !== scale) scale.value = clamp(Math.round(num(model.uiScale, 100)), SCALE_MIN, SCALE_MAX);
        previewScale(Number(scale.value));
        const distance = model.followDistance || 'normal';
        $('set-distance').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.getAttribute('data-value') === distance)));
        const spacing = SPACING[distance] || SPACING.normal;
        $('settings').querySelectorAll('.follower-dot').forEach((dot, i) => { dot.style.left = spacing[i] + 'px'; });
        const limit = $('set-limit');
        const max = clamp(Math.round(num(model.maxFollowers, 20)), 1, 20);
        if (document.activeElement !== limit) limit.value = max;
        previewLimit(Number(limit.value));
        $('set-input').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String((b.getAttribute('data-value') === 'keys') === !!model.useKeybinds)));
        [['set-sandbox', model.defaultSandboxEnabled], ['set-notify', model.notificationsEnabled]].forEach(([id, on]) => {
            const el = $(id);
            el.setAttribute('aria-checked', String(!!on));
            el.querySelector('span').textContent = on ? 'On' : 'Off';
        });
    }

    // ── Footer ───────────────────────────────────────────────────────────
    function renderOrders() {
        const any = model.followers.length > 0;
        ['order-summon', 'order-follow', 'order-wait', 'order-passive'].forEach(id => { $(id).disabled = !any; });
        $('order-passive').innerHTML = model.allPassive ? icon('swords') + 'All fight' : icon('shield-off') + 'All passive';
        $('order-passive').classList.toggle('tinted', !!model.allPassive);
    }

    function renderHints() {
        const box = $('hints');
        box.innerHTML = (ui.tab === 'followers' && model.followers.length ? '<span class="hint"><kbd>↑</kbd><kbd>↓</kbd>Browse</span>' : '') +
            '<span class="hint"><kbd>Esc</kbd>Close</span>';
    }

    // ── Tabs ─────────────────────────────────────────────────────────────
    function setTab(tab) {
        if (!['followers', 'dismissed', 'settings'].includes(tab)) return;
        const changed = ui.tab !== tab;
        ui.tab = tab;
        document.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-selected', String(t.getAttribute('data-tab') === tab)));
        ['followers', 'dismissed', 'settings'].forEach(name => {
            const view = $('view-' + name);
            view.hidden = name !== tab;
            if (name === tab && changed) {
                view.classList.remove('swap-in');
                void view.offsetWidth;
                view.classList.add('swap-in');
            }
        });
        if (tab === 'dismissed' && changed) send('hordeGetDismissed');
        render();
        renderHints();
    }

    // ── Toasts and confirmation ──────────────────────────────────────────
    function toast(html, iconName) {
        const box = $('toasts');
        const t = document.createElement('div');
        t.className = 'toast';
        t.innerHTML = icon(iconName || 'check') + '<span>' + html + '</span>';
        box.appendChild(t);
        while (box.children.length > 4) box.firstChild.remove();
        setTimeout(() => { t.classList.add('leave'); setTimeout(() => t.remove(), 260); }, 3200);
    }

    function confirmDialog(options) {
        const m = $('modal');
        const returnFocus = document.activeElement;
        m.innerHTML = '<div class="dialog" role="alertdialog" aria-modal="true" aria-labelledby="modal-title">' +
            '<div class="dialog-mark">' + icon('triangle-alert') + '</div><h2 id="modal-title">' + esc(options.title) + '</h2><p>' + options.body + '</p>' +
            '<div class="dialog-actions"><button class="btn" id="modal-cancel">Cancel</button>' +
            '<button class="btn danger" id="modal-confirm">' + icon(options.confirmIcon || 'check') + esc(options.confirm) + '</button></div></div>';
        m.hidden = false;
        m.classList.remove('leave');
        $('shell').inert = true;
        ui.modal = { onConfirm: options.onConfirm, returnFocus };
        $('modal-cancel').addEventListener('click', closeModal);
        $('modal-confirm').addEventListener('click', () => {
            const fn = ui.modal && ui.modal.onConfirm;
            closeModal();
            if (fn) fn();
        });
        m.onclick = e => { if (e.target === m) closeModal(); };
        $('modal-cancel').focus();
        renderHints();
    }

    function closeModal() {
        if (!ui.modal) return;
        const { returnFocus } = ui.modal;
        ui.modal = null;
        const m = $('modal');
        m.classList.add('leave');
        $('shell').inert = false;
        setTimeout(() => { if (!ui.modal) { m.hidden = true; m.innerHTML = ''; } }, 160);
        if (returnFocus && document.contains(returnFocus)) returnFocus.focus();
        renderHints();
    }

    // ── Native entry points ──────────────────────────────────────────────
    function render() {
        renderHeader();
        if (ui.tab === 'followers') { renderMuster(); renderRoster(); renderDossier(false); }
        else if (ui.tab === 'dismissed') renderDismissed();
        else renderSettings();
        renderOrders();
    }

    function flushAfterState() {
        const pending = ui.afterState.splice(0);
        pending.forEach(fn => { try { fn(); } catch (error) { console.error(error); } });
    }

    window.hordeUpdateState = function (state) {
        if (!state || typeof state !== 'object') return;
        if (typeof state.language === 'string' && state.language !== document.documentElement.lang) setLanguage(state.language);
        Object.assign(model, state);
        if ('uiScale' in state) applyScale(state.uiScale);
        model.followers = Array.isArray(state.followers) ? state.followers.filter(f => f && num(f.formID)) : [];
        if (Array.isArray(state.dismissed)) model.dismissed = state.dismissed.filter(d => d && num(d.formID));
        model.loaded = true;
        if (!followerById(ui.selected)) ui.selected = model.followers.length ? model.followers[0].formID : 0;
        render();
        renderHints();
        flushAfterState();
    };

    window.hordeUpdateDismissed = function (data) {
        model.dismissed = data && Array.isArray(data.dismissed) ? data.dismissed.filter(d => d && num(d.formID)) : [];
        renderHeader();
        if (ui.tab === 'dismissed') renderDismissed();
        renderHints();
        flushAfterState();
    };

    window.hordeShowPanel = function (formID) {
        const body = document.body;
        ui.open = true;
        if (formID) ui.selected = Number(formID);
        ui.arrive = true;
        closeModal();
        body.classList.remove('is-closing', 'is-opening');
        void body.offsetWidth;
        body.classList.add('is-open', 'is-opening');
        clearTimeout(window.hordeShowPanel._timer);
        window.hordeShowPanel._timer = setTimeout(() => body.classList.remove('is-opening'), 1600);
        $('app').inert = false;
        fit();
        replayEntrance();
        if (ui.tab !== 'followers') setTab('followers');
        else render();
        renderHints();
    };

    // Every open replays the show: gauges sweep up from zero, numbers count,
    // the radar grows and the roster cascades in.
    function replayEntrance() {
        document.querySelectorAll('.ring').forEach(ring => { ring._ratio = 0; });
        document.querySelectorAll('.ring-center b, #cap-count, #d-armor, #d-level').forEach(el => { el._value = 0; });
        radarFrom = SKILLS.map(() => 0);
        const perks = $('d-perks');
        if (perks) perks._key = '';
        Array.from($('roster').children).forEach((row, i) => {
            if (!row.classList.contains('row')) return;
            row.classList.remove('enter');
            void row.offsetWidth;
            row.style.setProperty('--i', i);
            row.classList.add('enter');
        });
    }

    window.hordeHidePanel = function () {
        ui.open = false;
        closeModal();
        document.body.classList.remove('is-open', 'is-opening', 'is-closing');
        $('app').inert = true;
    };

    function requestClose(reason) {
        if (!ui.open) return;
        if (ui.modal) { closeModal(); return; }
        ui.open = false;
        document.body.classList.add('is-closing');
        setTimeout(() => {
            if (send('hordeClose', { reason: reason })) return;
            // Without the native close, hiding would leave Skyrim paused behind
            // an invisible menu. Stay open and say why.
            document.body.classList.remove('is-closing');
            ui.open = true;
            toast('Horde can’t close: its game connection is missing. Check Magelight.log.', 'circle-alert');
        }, 170);
    }

    // ── Pointer effects ──────────────────────────────────────────────────
    // Ultralight (WebKit zoom) reports rects in page pixels; Chrome in screen pixels.
    function localPoint(el, e) {
        const r = el.getBoundingClientRect(), z = ui.zoom;
        const screenRects = z === 1 || r.width > el.offsetWidth * 1.01;
        return screenRects ? [(e.clientX - r.left) / z, (e.clientY - r.top) / z] : [e.clientX / z - r.left, e.clientY / z - r.top];
    }
    function spotlight(e) {
        const panel = e.target.closest && e.target.closest('.panel');
        if (!panel) return;
        const [x, y] = localPoint(panel, e);
        panel.style.setProperty('--mx', x.toFixed(0) + 'px');
        panel.style.setProperty('--my', y.toFixed(0) + 'px');
    }
    function burst(button) {
        // Table cells are offset parents without being positioned; climb to a real container.
        let host = button.offsetParent, x = button.offsetLeft + button.offsetWidth / 2, y = button.offsetTop + button.offsetHeight / 2;
        while (host && getComputedStyle(host).position === 'static') {
            x += host.offsetLeft;
            y += host.offsetTop;
            host = host.offsetParent;
        }
        if (!host) return;
        const b = document.createElement('div');
        b.className = 'burst' + (button.classList.contains('danger') ? ' danger' : '');
        b.style.left = x + 'px';
        b.style.top = y + 'px';
        let html = '';
        for (let i = 0; i < 22; i++) {
            const angle = Math.random() * Math.PI * 2, reach = 40 + Math.random() * 70;
            html += '<i style="--bx:' + (Math.cos(angle) * reach).toFixed(0) + 'px;--by:' + (Math.sin(angle) * reach * 0.7 - 12).toFixed(0) +
                'px;--bs:' + (3 + Math.random() * 3.5).toFixed(1) + 'px;--bd:' + (560 + Math.random() * 420).toFixed(0) + 'ms"></i>';
        }
        b.innerHTML = html;
        host.appendChild(b);
        setTimeout(() => b.remove(), 1050);
    }

    // ── Startup ──────────────────────────────────────────────────────────
    hydrateIcons(document);
    buildRail();
    sparks();
    fit();
    window.addEventListener('resize', fit);
    document.querySelectorAll('.tab').forEach(t => t.addEventListener('click', () => setTab(t.getAttribute('data-tab'))));
    $('sort').addEventListener('click', e => {
        const b = e.target.closest('button[data-sort]');
        if (!b) return;
        ui.sort = b.getAttribute('data-sort');
        $('sort').querySelectorAll('button').forEach(x => x.setAttribute('aria-pressed', String(x === b)));
        renderRoster();
    });
    $('btn-close').addEventListener('click', () => requestClose('button'));
    $('dismissed').addEventListener('click', onDismissedClick);
    $('order-summon').addEventListener('click', () => order('hordeSummonAll', {}, () => toast('Summoned your followers.', 'locate-fixed')));
    $('order-follow').addEventListener('click', () => order('hordeFollowAll', {}, () => toast('Everyone is following you.', 'footprints')));
    $('order-wait').addEventListener('click', () => order('hordeWaitAll', {}, () => toast('Everyone is waiting here.', 'hourglass')));
    $('order-passive').addEventListener('click', () => {
        const passive = !model.allPassive;
        order('hordePassiveAll', { passive }, () => toast(passive ? 'Your followers won’t start fights.' : 'Your followers will fight.', passive ? 'shield-off' : 'swords'));
    });
    // Keyboard navigation shows focus rings; a click hides them and moves
    // focus to what was clicked, so no stale ring is left on another control.
    window.addEventListener('keydown', e => {
        if (['Tab', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', ' '].includes(e.key)) {
            document.documentElement.classList.add('keyboard-nav');
        }
    }, true);
    document.addEventListener('mousedown', e => {
        document.documentElement.classList.remove('keyboard-nav');
        const target = e.target.closest && e.target.closest('button');
        if (target && !target.disabled) target.focus({ preventScroll: true });
    }, true);
    document.addEventListener('mousemove', spotlight);
    document.addEventListener('click', e => {
        const button = e.target.closest && e.target.closest('.btn, .seg button');
        if (button && !button.disabled && button.getAttribute('aria-pressed') !== 'true') burst(button);
    });
    window.addEventListener('keydown', e => {
        if (!ui.open || ui.modal || ui.tab !== 'followers' || (e.key !== 'ArrowDown' && e.key !== 'ArrowUp')) return;
        if (document.activeElement && document.activeElement !== document.body) return;
        const row = rows.get(ui.selected) || $('roster').querySelector('.row');
        if (row) { row.focus(); e.preventDefault(); }
    });
    // Ultralight reports Escape as key "Unidentified"; its keyCode is still 27.
    window.addEventListener('keydown', e => {
        if ((e.key !== 'Escape' && e.keyCode !== 27) || !ui.open) return;
        e.preventDefault();
        e.stopPropagation();
        if (ui.modal) closeModal();
        else requestClose('escape');
    }, true);
    render();
    renderHints();

    // Browser preview: dev/mock.js stands in for HordeUI. Not shipped.
    if (window.__hordeMock || /[?&]mock\b/.test(location.search)) {
        const script = document.createElement('script');
        script.src = 'dev/mock.js';
        document.body.appendChild(script);
    }
})();
