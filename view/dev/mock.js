// Development stand-in for HordeUI's native listeners. Loaded only when the
// page runs with ?mock (browser) or window.__hordeMock (Ultralight harness).
// Options: ?empty starts with no followers, ?lang=ru sends a game language,
// ?intl uses names in other scripts.
(function () {
    'use strict';
    const options = Object.assign({
        empty: /[?&]empty\b/.test(location.search),
        lang: (/[?&]lang=([\w-]+)/.exec(location.search) || [])[1],
        intl: /[?&]intl\b/.test(location.search)
    }, window.__hordeMockOptions || {});
    document.body.classList.add('dev-backdrop');

    const gear = (right, left, head, body, hands, feet) => {
        const slot = s => { const [name, stat, isWeapon] = s || []; return { name: name || 'None', stat: stat || 0, isWeapon: !!isWeapon }; };
        return { rightHand: slot(right), leftHand: slot(left), head: slot(head), body: slot(body), hands: slot(hands), feet: slot(feet) };
    };
    const skills = (oneHanded, twoHanded, archery, block, heavyArmor, lightArmor, destruction, restoration, conjuration) =>
        ({ oneHanded, twoHanded, archery, block, heavyArmor, lightArmor, destruction, restoration, conjuration });
    const follower = (formID, name, race, className, level, vitals, extra) => Object.assign({
        formID, name, race, className, level,
        health: vitals[0], healthMax: vitals[1], magicka: vitals[2], magickaMax: vitals[3], stamina: vitals[4], staminaMax: vitals[5],
        isSandboxEnabled: false, isPassive: false, isWaiting: false, isFollowClose: false, isAnimal: false, isEssential: true,
        homeWorldspace: 0, homeName: '', distance: 3, location: 'Whiterun Hold', armorRating: 0, perks: []
    }, extra);

    const state = {
        maxFollowers: 20, defaultSandboxEnabled: false, notificationsEnabled: true, followDistance: 'normal', useKeybinds: true, allPassive: false, uiScale: 100,
        followers: options.empty ? [] : [
            follower(0x000A2C94, 'Lydia', 'Nord', 'Housecarl', 32, [418, 460, 90, 90, 240, 260], {
                distance: 3, armorRating: 312, isFollowClose: true, homeWorldspace: 0x3C, homeName: 'Breezehome',
                skills: skills(68, 41, 32, 64, 71, 20, 12, 15, 10),
                equipment: gear(['Steel Sword', 8, true], ['Steel Shield', 24], ['Steel Helmet', 17], ['Steel Armor', 25], ['Steel Gauntlets', 10], ['Steel Boots', 10]),
                perks: ['Armsman', 'Bladesman', 'Shield Wall', 'Deflect Arrows', 'Juggernaut', 'Well Fitted', 'Fists of Steel']
            }),
            follower(0x02002B74, 'Serana', 'Nord', 'Vampire', 40, [355, 380, 412, 430, 180, 210], {
                distance: 6, armorRating: 96, location: 'Whiterun Hold',
                skills: skills(32, 18, 22, 25, 15, 38, 74, 52, 81),
                equipment: gear(['Daedric Dagger', 11, true], ['Drain Life', 0], null, ['Royal Armor', 26], null, ['Royal Boots', 6]),
                perks: ['Novice Destruction', 'Apprentice Destruction', 'Adept Destruction', 'Augmented Frost', 'Novice Conjuration', 'Summoner', 'Necromancy', 'Dark Souls', 'Twin Souls', 'Atromancy', 'Mystic Binding', 'Regeneration', 'Recovery', 'Respite', 'Agile Defender', 'Custom Fit', 'Matching Set', 'Deft Movement', 'Dual Casting', 'Rune Master']
            }),
            follower(0x0001C196, "J'zargo", 'Khajiit', 'Mage', 28, [205, 240, 330, 330, 120, 140], {
                distance: 11, armorRating: 48, isPassive: true, location: 'Whiterun Hold',
                skills: skills(22, 12, 15, 10, 8, 26, 76, 40, 48),
                equipment: gear(['Fireball', 0], ['Lightning Bolt', 0], ['Hood', 0], ['Mage Robes', 0], null, ['Boots', 2]),
                perks: ['Novice Destruction', 'Apprentice Destruction', 'Augmented Flames', 'Impact', 'Recovery']
            }),
            follower(0x0001A697, 'Aela the Huntress', 'Nord', 'Archer', 34, [104, 390, 60, 60, 310, 330], {
                distance: 38, armorRating: 140, isSandboxEnabled: true, location: 'Whiterun Hold',
                skills: skills(40, 20, 82, 30, 18, 66, 10, 12, 8),
                equipment: gear(['Ancient Nord Bow', 13, true], ['Dagger', 6, true], null, ['Ancient Shrouded Armor', 32], ['Hide Bracers', 5], ['Hide Boots', 5]),
                perks: ['Overdraw', 'Eagle Eye', 'Steady Hand', 'Power Shot', 'Quick Shot', 'Agile Defender', 'Wind Walker']
            }),
            follower(0x00013BAB, 'Meeko', 'Dog', 'Animal', 9, [120, 150, 0, 0, 90, 90], {
                distance: 2, isAnimal: true, isEssential: false, skills: skills(0, 0, 0, 0, 0, 0, 0, 0, 0)
            }),
            follower(0x0001A6D9, 'Vilkas', 'Nord', 'Warrior', 36, [520, 560, 60, 60, 300, 320], {
                distance: 410, isWaiting: true, armorRating: 250, location: 'Whiterun', homeWorldspace: 0x3C, homeName: 'Jorrvaskr',
                skills: skills(40, 84, 20, 35, 70, 22, 8, 10, 6),
                equipment: gear(['Skyforge Steel Greatsword', 21, true], null, null, ['Wolf Armor', 31], ['Wolf Gauntlets', 11], ['Wolf Boots', 11]),
                perks: ['Barbarian', 'Champion’s Stance', 'Devastating Blow', 'Great Critical Charge', 'Juggernaut']
            }),
            follower(0x000A2C8E, 'Mjoll the Lioness', 'Nord', 'Warrior', 30, [330, 400, 70, 70, 220, 260], {
                distance: 1840, isWaiting: true, armorRating: 220, location: 'Riften', isPassive: false,
                skills: skills(66, 30, 18, 58, 60, 20, 6, 22, 5),
                equipment: gear(['Grimsever', 16, true], ['Steel Shield', 24], null, ['Steel Plate Armor', 40], ['Steel Plate Gauntlets', 13], ['Steel Plate Boots', 13]),
                perks: ['Armsman', 'Fighting Stance', 'Shield Wall', 'Quick Reflexes']
            })
        ],
        dismissed: options.empty ? [] : [
            { formID: 0x0001348F, name: 'Faendal', homeName: 'Riverwood', hasHome: true },
            { formID: 0x000918E2, name: 'Uthgerd the Unbroken', homeName: '', hasHome: false },
            { formID: 0x00065657, name: 'Erik the Slayer', homeName: 'Rorikstead', hasHome: true }
        ]
    };
    state.followers.forEach(f => { f.equipment = f.equipment || gear(); f.skills = f.skills || skills(0, 0, 0, 0, 0, 0, 0, 0, 0); });
    if (options.lang) state.language = options.lang;
    if (options.intl) {
        // Native replaces bytes that are not UTF-8 with U+FFFD (the last name).
        ['Лидия', '瑟拉娜', 'ジェイ・ザルゴ', 'Αίλα η Κυνηγός', 'Μίκο', 'Вилкас', '�owyn'].forEach((name, i) => {
            if (state.followers[i]) state.followers[i].name = name;
        });
        state.followers.forEach(f => { f.location = 'Вайтран'; });
        state.dismissed = options.empty ? [] : [
            { formID: 0x0001348F, name: 'Ёрлунд Седая Грива', homeName: 'Вайтран', hasHome: true },
            { formID: 0x000918E2, name: 'Утгерд Несломленная', homeName: '', hasHome: false },
            { formID: 0x00065657, name: 'Бранд-Шей', homeName: 'Рифтен', hasHome: true }
        ];
    }

    const find = id => state.followers.find(f => f.formID === id);
    const push = () => setTimeout(() => {
        state.count = state.followers.length;
        state.dismissedCount = state.dismissed.length;
        window.hordeUpdateState(JSON.parse(JSON.stringify(state)));
    }, 90);
    const handlers = {};
    const listener = (name, fn) => { handlers[name] = fn; };
    // Stands in for Magelight's page bridge, which routes send() to HordeUI.
    window.magelight = Object.freeze({
        send(name, json) {
            const fn = handlers[name];
            if (!fn) return;
            const data = JSON.parse(json || '{}');
            console.log('[mock] ' + name, json);
            fn(data, find(Number(data.formID)));
            push();
        }
    });
    const here = 'Whiterun Hold';

    listener('hordeGetState', () => {});
    listener('hordeGetDismissed', () => {});
    listener('hordeClose', () => setTimeout(() => { window.hordeHidePanel(); reopen.hidden = false; }, 10));
    listener('hordeSetFollow', (d, f) => { if (f) f.isWaiting = false; });
    listener('hordeSetWait', (d, f) => { if (f) f.isWaiting = true; });
    listener('hordeSetPassive', (d, f) => { if (f) f.isPassive = !!d.passive; });
    listener('hordeSetSandbox', (d, f) => { if (f) f.isSandboxEnabled = !!d.enabled; });
    listener('hordeSetFollowClose', (d, f) => { if (f) f.isFollowClose = !!d.enabled; });
    listener('hordeSetEssential', (d, f) => { if (f) f.isEssential = !!d.essential; });
    listener('hordeSummon', (d, f) => { if (f) { f.distance = 2; f.location = here; } });
    listener('hordeSummonAll', () => state.followers.forEach(f => { f.distance = 2 + Math.round(Math.random() * 3); f.location = here; }));
    listener('hordeFollowAll', () => state.followers.forEach(f => { f.isWaiting = false; }));
    listener('hordeWaitAll', () => state.followers.forEach(f => { f.isWaiting = true; }));
    listener('hordePassiveAll', d => { state.allPassive = !!d.passive; state.followers.forEach(f => { f.isPassive = state.allPassive; }); });
    listener('hordeDismiss', (d, f) => {
        if (!f) return;
        state.followers = state.followers.filter(x => x !== f);
        state.dismissed.push({ formID: f.formID, name: f.name, homeName: f.homeName, hasHome: !!f.homeWorldspace });
    });
    listener('hordeSetHome', (d, f) => { if (f) { f.homeWorldspace = 0x3C; f.homeName = 'Dragonsreach'; } });
    listener('hordeClearHome', (d, f) => { if (f) { f.homeWorldspace = 0; f.homeName = ''; } });
    listener('hordeUpdateSettings', d => {
        ['maxFollowers', 'defaultSandboxEnabled', 'notificationsEnabled', 'followDistance', 'uiScale'].forEach(k => { if (k in d) state[k] = d[k]; });
    });
    listener('hordeToggleInputMode', () => { state.useKeybinds = !state.useKeybinds; });
    const registry = id => state.dismissed.find(x => x.formID === id);
    listener('hordeSummonDismissed', () => {});
    listener('hordeSetDismissedHome', d => { const r = registry(Number(d.formID)); if (r) { r.hasHome = true; r.homeName = 'Dragonsreach'; } });
    listener('hordeClearDismissedHome', d => { const r = registry(Number(d.formID)); if (r) { r.hasHome = false; r.homeName = ''; } });
    listener('hordeForgetFollower', d => { state.dismissed = state.dismissed.filter(x => x.formID !== Number(d.formID)); });

    const reopen = document.createElement('button');
    reopen.textContent = 'Open Horde';
    reopen.hidden = true;
    reopen.style.cssText = 'position:fixed;left:24px;bottom:24px;z-index:99;padding:10px 16px;font:600 14px Saira Condensed,sans-serif;letter-spacing:.12em;text-transform:uppercase;color:#14110f;background:#f4761f;border:0;border-radius:4px';
    reopen.addEventListener('click', open);
    document.body.appendChild(reopen);

    function open() {
        reopen.hidden = true;
        window.hordeShowPanel();
        push();
    }
    window.__hordeMockState = state;
    setTimeout(open, 60);
})();
