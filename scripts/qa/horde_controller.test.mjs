import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { chromium } from 'playwright';

const helperPath = process.env.MERIDIAN_INPUT_HELPER;
assert.ok(helperPath, 'Set MERIDIAN_INPUT_HELPER to Meridian src/UIPlatform/Web/meridian-input.js');
const helper = fs.readFileSync(helperPath, 'utf8');
let browser;
before(async () => { browser = await chromium.launch({ headless: true }); });
after(async () => { await browser?.close(); });

const follower = (formID, name) => ({ formID, name, level: 20, className: 'Warrior', health: 100,
    healthMax: 100, magicka: 50, magickaMax: 50, stamina: 100, staminaMax: 100, homeWorldspace: 1 });
const state = { followers: [follower(101, 'Lydia'), follower(202, 'Aela'), follower(303, 'Iona')],
    count: 3, maxFollowers: 20, dismissedCount: 2 };

async function setup(t, { fallback = false, data = state } = {}) {
    const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
    page.setDefaultTimeout(3000);
    t.after(() => page.close());
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    t.after(() => assert.deepEqual(errors, [], 'No page errors'));
    if (!fallback) await page.addInitScript({ content: helper + `
        window.inputRequests=[];
        __meridianInstallInput((token,name,payload)=>inputRequests.push(JSON.parse(payload)), 'test');
    ` });
    await page.goto(new URL('../../view/index.html', import.meta.url).href);
    await page.evaluate(({ data, fallback }) => {
        window.calls = [];
        for (const name of ['hordeClose','hordeSetWait','hordeSetFollow','hordeSummon','hordeSetPassive',
            'hordeSetFollowClose','hordeSetSandbox','hordeSetEssential','hordeDismiss','hordeSetHome','hordeClearHome',
            'hordeSummonAll','hordeFollowAll','hordeWaitAll','hordePassiveAll','hordeUpdateSettings','hordeToggleInputMode',
            'hordeGetDismissed','hordeSummonDismissed','hordeSetDismissedHome','hordeClearDismissedHome','hordeForgetFollower']) {
            window[name] = payload => calls.push({ name, payload });
        }
        window.testState = data;
        hordeUpdateState(data);
        hordeShowPanel();
        hordeUpdateState(data);
        hordeUpdateDismissed({ dismissed: [{ formID: 404, name: 'Jenassa', hasHome: true, homeName: 'Whiterun' },
            { formID: 505, name: 'Uthgerd', hasHome: false }] });
        if (!fallback) {
            window.sequence = 0;
            window.packetState = { enabled: true, active: true, connected: true, mode: 'navigation',
                device: 'gamepad', glyphFamily: 'xbox', generation: 1,
                bindings: { up:'dpadUp', down:'dpadDown', left:'dpadLeft', right:'dpadRight', accept:'south',
                    cancel:'east', previousTab:'leftShoulder', nextTab:'rightShoulder', secondary:'west',
                    tertiary:'north', toggleCursor:'rightThumb' } };
            window.send = (action = 'accept', phase = 'press', extra = {}, control = 'south') => {
                Object.assign(packetState, extra);
                MeridianInput.__receive({ version: 1, page: inputRequests[0].page, sequence: ++sequence,
                    dt: .05, ...packetState, events: [{ action, phase, control, x: 0, y: 0, value: 1 }] });
            };
            send('none', 'change');
        }
    }, { data, fallback });
    return page;
}
const selected = page => page.evaluate(() => document.activeElement.dataset.meridianId || document.activeElement.id);
const send = (page, action = 'accept', phase = 'press', extra = {}) => page.evaluate(
    ({ action, phase, extra }) => window.send(action, phase, extra), { action, phase, extra });
const focus = (page, id) => page.locator(`[data-meridian-id="${id}"],#${id}`).focus();
const calls = page => page.evaluate(() => window.calls);

test('roster selection, sections, commands and stable refresh', async t => {
    const page = await setup(t);
    assert.equal(await selected(page), 'follower-101');
    await send(page, 'down', 'repeat');
    assert.equal(await selected(page), 'follower-202');
    await send(page);
    assert.equal(await selected(page), 'detail-202-follow');
    await focus(page, 'detail-202-wait');
    await send(page);
    assert.deepEqual(await calls(page), [{ name: 'hordeSetWait', payload: '{"formID":202}' }]);
    await page.evaluate(() => hordeUpdateState({ ...testState, followers: [...testState.followers].reverse() }));
    assert.equal(await selected(page), 'detail-202-wait');
    await send(page, 'cancel');
    assert.equal(await selected(page), 'follower-202');
    await send(page, 'nextTab');
    assert.equal(await selected(page), 'detail-202-wait');
    await send(page, 'nextTab');
    assert.equal(await selected(page), 'group-summon');
    await send(page, 'nextTab');
    assert.equal(await selected(page), 'distance-close');
    await send(page, 'nextTab');
    assert.equal(await selected(page), 'follower-202');
});

test('destructive confirmation starts on Cancel and back never closes Horde', async t => {
    const page = await setup(t);
    await send(page);
    await focus(page, 'detail-101-dismiss');
    await send(page);
    assert.equal(await selected(page), 'modalCancel');
    await send(page, 'nextTab');
    assert.equal(await selected(page), 'modalCancel');
    await send(page, 'cancel');
    await send(page); // Must not fall through during the closing animation.
    assert.equal(await page.locator('#panel').evaluate(e => e.inert), true);
    await page.waitForTimeout(200);
    assert.deepEqual(await calls(page), []);
    assert.equal(await selected(page), 'detail-101-dismiss');
    await send(page);
    await send(page, 'right');
    assert.equal(await selected(page), 'modalConfirm');
    await send(page);
    await send(page);
    assert.equal((await calls(page)).filter(c => c.name === 'hordeDismiss').length, 1);
});

test('old Meridian keeps mouse/keyboard controls without controller hints', async t => {
    const page = await setup(t, { fallback: true });
    await page.locator('.row-active').nth(1).click();
    await page.getByRole('button', { name: 'Wait', exact: true }).click();
    await page.keyboard.press('Escape');
    assert.deepEqual((await calls(page)).map(c => c.name), ['hordeSetWait','hordeClose']);
    assert.equal(await page.locator('#controllerHints').isVisible(), false);
});

test('all follower controls dispatch existing callbacks once with the selected FormID', async t => {
    const page = await setup(t);
    await send(page);
    for (const control of ['follow', 'wait', 'passive', 'followClose', 'sandbox', 'essential', 'summon', 'home']) {
        await focus(page, 'detail-101-' + control);
        await send(page);
        await send(page, 'accept', 'repeat');
        await send(page, 'accept', 'release');
    }
    const actual = await calls(page);
    assert.deepEqual(actual.map(c => c.name), ['hordeSetFollow', 'hordeSetWait', 'hordeSetPassive',
        'hordeSetFollowClose', 'hordeSetSandbox', 'hordeSetEssential', 'hordeSummon', 'hordeSetHome']);
    assert.ok(actual.every(c => JSON.parse(c.payload).formID === 101));
    assert.equal(JSON.parse(actual[2].payload).passive, true);
    assert.equal(JSON.parse(actual[3].payload).enabled, true);
    assert.equal(JSON.parse(actual[4].payload).enabled, true);
    assert.equal(JSON.parse(actual[5].payload).essential, false);
    await focus(page, 'detail-101-clearHome');
    await send(page);
    assert.equal(await selected(page), 'modalCancel');
    await page.evaluate(() => hordeUpdateState(testState));
    assert.equal(await selected(page), 'modalCancel');
    await send(page, 'right');
    await send(page);
    assert.equal((await calls(page)).at(-1).name, 'hordeClearHome');
});

test('group orders and standing settings are reachable and repeat-safe', async t => {
    const page = await setup(t);
    await send(page, 'secondary');
    for (const id of ['group-summon', 'group-follow', 'group-wait', 'btnPassiveAll']) {
        await focus(page, id);
        await send(page);
        await send(page, 'accept', 'repeat');
    }
    await send(page, 'nextTab');
    for (const id of ['distance-close', 'distance-normal', 'distance-far', 'muteNotifications', 'btnInputMode']) {
        await focus(page, id);
        await send(page);
    }
    const actual = await calls(page);
    assert.deepEqual(actual.map(c => c.name), ['hordeSummonAll','hordeFollowAll','hordeWaitAll','hordePassiveAll',
        'hordeUpdateSettings','hordeUpdateSettings','hordeUpdateSettings','hordeUpdateSettings','hordeToggleInputMode']);
    assert.deepEqual(actual.slice(4, 7).map(c => JSON.parse(c.payload).followDistance), ['close','normal','far']);
    assert.equal(JSON.parse(actual[7].payload).notificationsEnabled, false);
});

test('registry supports commands, disabled home clearing, confirmation, refresh and back', async t => {
    const page = await setup(t);
    await send(page, 'tertiary');
    assert.equal(await selected(page), 'registry-404-summon');
    for (const id of ['summon', 'home', 'clearHome']) {
        await focus(page, 'registry-404-' + id);
        await send(page);
    }
    assert.equal(await page.locator('[data-meridian-id="registry-505-clearHome"]').isDisabled(), true);
    await focus(page, 'registry-505-home');
    await send(page, 'right');
    assert.equal(await selected(page), 'registry-505-forget');
    await send(page);
    await page.evaluate(() => hordeUpdateDismissed(_dismissedState));
    assert.equal(await selected(page), 'modalCancel');
    await send(page, 'cancel');
    await page.waitForTimeout(180);
    assert.equal(await selected(page), 'registry-505-forget');
    await send(page);
    await send(page, 'right');
    await send(page);
    await page.waitForTimeout(180);
    await send(page, 'cancel');
    assert.equal(await selected(page), 'btnDismissed');
    assert.deepEqual((await calls(page)).map(c => c.name), ['hordeGetDismissed','hordeSummonDismissed',
        'hordeSetDismissedHome','hordeClearDismissedHome','hordeForgetFollower']);
});

test('focus follows identity through reorder, filtering, removal and empty roster', async t => {
    const page = await setup(t);
    await send(page, 'down');
    await page.evaluate(() => hordeUpdateState({ ...testState, followers: [...testState.followers].reverse() }));
    assert.equal(await selected(page), 'follower-202');
    await page.evaluate(() => hordeUpdateState({ ...testState, followers: [testState.followers[0]] }));
    assert.equal(await selected(page), 'follower-101');
    await page.evaluate(() => hordeUpdateState({ followers: [], count: 0, maxFollowers: 20, dismissedCount: 0 }));
    assert.equal(await selected(page), 'grid');
    await send(page);
    await send(page, 'secondary');
    assert.equal(await selected(page), 'groupOrders');
    assert.equal(await page.locator('#btnPassiveAll').isDisabled(), true);
    await send(page);
    await send(page, 'nextTab');
    assert.equal(await selected(page), 'distance-close');
    assert.deepEqual(await calls(page), []);
});

test('large roster scrolls and crosshair quick-open selects the requested follower', async t => {
    const page = await setup(t, { data: { ...state, count: 20,
        followers: Array.from({ length: 20 }, (_, i) => follower(i + 1, 'Follower ' + (i + 1))) } });
    // At normal aspect ratios all 20 rows fit. Exercise the scroll path in a
    // short viewport where the minimum readable font size forces overflow.
    await page.setViewportSize({ width: 1280, height: 400 });
    for (let i = 0; i < 19; i++) await send(page, 'down', 'repeat');
    assert.equal(await selected(page), 'follower-20');
    assert.ok(await page.locator('#grid').evaluate(e => e.scrollTop > 0));
    await page.evaluate(() => { hordeHidePanel(); hordeShowPanel(15); hordeUpdateState(testState); });
    assert.equal(await selected(page), 'follower-15');
    await send(page);
    assert.equal(await selected(page), 'detail-15-follow');
});

test('close disables pending input, cleans modal, and repeated reopen does not duplicate handlers', async t => {
    const page = await setup(t);
    await send(page, 'cancel');
    await send(page, 'cancel');
    await send(page);
    assert.equal((await calls(page)).length, 1);
    assert.equal(await page.locator('#controllerHints').isVisible(), false);
    for (let i = 0; i < 3; i++) await page.evaluate(() => { hordeShowPanel(); hordeUpdateState(testState); });
    await send(page);
    await focus(page, 'detail-101-dismiss');
    await send(page);
    await page.evaluate(() => { hordeHidePanel(); hordeShowPanel(); hordeUpdateState(testState); });
    assert.equal(await page.locator('#modalOverlay').isVisible(), false);
    await send(page, 'cancel');
    assert.equal((await calls(page)).filter(c => c.name === 'hordeClose').length, 2);
    assert.equal((await calls(page)).filter(c => c.name === 'hordeDismiss').length, 0);
});

test('inactive/disconnected input is harmless and reconnect restores navigation', async t => {
    const page = await setup(t);
    await send(page, 'accept', 'press', { active: false, connected: false, generation: 2 });
    assert.equal(await page.locator('#controllerHints').isVisible(), false);
    await send(page, 'secondary');
    await send(page, 'cancel');
    assert.deepEqual(await calls(page), []);
    await send(page, 'none', 'change', { active: true, connected: true, generation: 3 });
    assert.equal(await page.locator('#controllerHints').isVisible(), true);
    await send(page);
    assert.equal(await selected(page), 'detail-101-follow');
});

test('cursor accept does not double-activate and mouse handoff adopts its section', async t => {
    const page = await setup(t);
    await send(page, 'accept', 'press', { mode: 'cursor' });
    assert.equal(await selected(page), 'follower-101');
    await page.getByRole('button', { name: 'Wait', exact: true }).click();
    assert.deepEqual((await calls(page)).map(c => c.name), ['hordeSetWait']);
    await send(page, 'none', 'change', { device: 'keyboardMouse' });
    assert.equal(await page.locator('#controllerHints').isVisible(), false);
    await send(page, 'none', 'change', { device: 'gamepad', mode: 'navigation' });
    assert.equal(await selected(page), 'detail-101-wait');
    await send(page);
    assert.equal((await calls(page)).length, 2);
});

test('prompts use current bindings and glyph family without hardcoded face labels', async t => {
    const page = await setup(t);
    assert.match(await page.locator('#controllerHints').innerText(), /A Select follower/);
    await send(page, 'none', 'change', { glyphFamily: 'playstation' });
    assert.match(await page.locator('#controllerHints').innerText(), /Cross Select follower/);
    await page.evaluate(() => send('none', 'change', { bindings: { ...packetState.bindings, accept: 'north', tertiary: 'none' } }));
    const text = await page.locator('#controllerHints').innerText();
    assert.match(text, /Triangle Select follower/);
    assert.doesNotMatch(text, /Dismissed/);
    assert.equal(await page.locator('#controllerHints').evaluate(e => e.scrollWidth <= e.clientWidth), true);
});

test('helper injected after page scripts is acquired on native-ready/open', async t => {
    const page = await setup(t, { fallback: true });
    await page.addScriptTag({ content: helper });
    await page.evaluate(() => {
        window.requests = [];
        __meridianInstallInput((t, n, p) => requests.push(JSON.parse(p)), 'late');
        HordeController.initialize();
        hordeShowPanel(202);
        hordeUpdateState(testState);
    });
    assert.equal(await selected(page), 'follower-202');
});

test('native opt-in remains optional, conflict-checked and uses the paused close lifecycle', () => {
    const source = relative => fs.readFileSync(new URL('../../' + relative, import.meta.url), 'utf8');
    const main = source('src/main.cpp');
    const native = source('src/ui/HordeUI.cpp');
    const controller = source('view/controller.js');
    const readme = source('README.md');
    assert.match(main, /OnInputLoaded\(\)[\s\S]*?Meridian::UI::Input::Query/);
    assert.match(native, /if \(!g_MeridianInput\) return/);
    assert.match(native, /config.enabled = 1/);
    assert.match(native, /config.allowCursor = 1/);
    assert.match(native, /shortcut.modifier = Control::LeftShoulder/);
    assert.match(native, /shortcut.button = Control::North/);
    assert.doesNotMatch(native, /shortcut.button = Control::Back/);
    assert.match(readme, /Hold LB, then press Y/);
    assert.match(native, /Result::Conflict/);
    assert.match(native, /FocusMode::PauseGame/);
    assert.match(native, /hordeHidePanel[\s\S]*?Unfocus\(_view\)[\s\S]*?Hide\(_view\)/);
    assert.doesNotMatch(native + controller, /XInputGetState|navigator\.getGamepads|SendInput\(/);
});

test('rendered focus and hints fit the interface at common viewport sizes', async t => {
    const page = await setup(t);
    for (const [width, height] of [[1280, 720], [1920, 1080], [2560, 1080]]) {
        await page.setViewportSize({ width, height });
        await page.waitForTimeout(50);
        assert.equal(await page.locator('#controllerHints').evaluate(e => e.scrollWidth <= e.clientWidth), true);
        const panel = await page.locator('#panel').boundingBox();
        const hints = await page.locator('#controllerHints').boundingBox();
        assert.ok(panel.y + panel.height <= hints.y + 1, 'Footer must not cover controls');
    }
    if (process.env.HORDE_CONTROLLER_SCREENSHOT) {
        await page.setViewportSize({ width: 1600, height: 900 });
        await page.screenshot({ path: process.env.HORDE_CONTROLLER_SCREENSHOT });
    }
});

test('cursor clicks cannot reach commands behind a closing confirmation', async t => {
    const page = await setup(t);
    await send(page);
    const box = await page.locator('[data-meridian-id="detail-101-summon"]').boundingBox();
    await focus(page, 'detail-101-dismiss');
    await send(page);
    await send(page, 'cancel');
    await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    assert.deepEqual(await calls(page), []);
    await page.waitForTimeout(180);
    assert.equal(await page.locator('#panel').evaluate(e => e.inert), false);
});

test('right-stick scrolling uses the shared helper on a constrained roster', async t => {
    const page = await setup(t, { data: { ...state, count: 20,
        followers: Array.from({ length: 20 }, (_, i) => follower(i + 1, 'Follower ' + (i + 1))) } });
    await page.setViewportSize({ width: 1280, height: 400 });
    await page.evaluate(() => {
        MeridianInput.__receive({ version: 1, page: inputRequests[0].page, sequence: ++sequence,
            dt: .05, ...packetState, events: [{ action: 'none', phase: 'change', control: 'rightStick', x: 0, y: -1, value: 0 }] });
    });
    assert.ok(await page.locator('#grid').evaluate(e => e.scrollTop > 0));
    assert.equal(await selected(page), 'follower-1');
    assert.deepEqual(await calls(page), []);
});
