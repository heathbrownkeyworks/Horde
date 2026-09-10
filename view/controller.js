(function () {
    'use strict';

    const sections = ['followers', 'commands', 'orders', 'settings'];
    const labels = { followers: 'Followers', commands: 'Commands', orders: 'Group Orders', settings: 'Settings' };
    let input = null;
    let opened = false;
    let section = 'followers';
    let saved = {};
    let scopeRoot = null;
    let stopScope = null;
    let stopModal = null;
    let stopState = null;
    let stopActions = null;
    const byId = id => document.getElementById(id);
    const identity = element => element?.dataset.meridianId || element?.id || '';
    const modalVisible = () => byId('modalOverlay').classList.contains('open');
    const gamepad = () => input?.getState().device === 'gamepad';

    function rootFor(name) {
        if (name === 'followers') return byId('grid');
        if (name === 'commands') return byId(window._currentView === 'dismissed' ? 'viewDismissed' : 'viewDetail');
        return byId(name === 'orders' ? 'groupOrders' : 'footer');
    }

    function candidates(root) {
        const controls = Array.from(root.querySelectorAll('button,input,[role="button"]'))
            .filter(element => !element.disabled && element.tabIndex >= 0 &&
                !element.closest('[inert],[hidden],.hidden') && element.getClientRects().length > 0);
        // An empty roster/registry remains a navigable section, with working Back
        // and shoulder actions but no command that can accidentally be invoked.
        const tabIndex = controls.length ? -1 : 0;
        if (root.tabIndex !== tabIndex) root.tabIndex = tabIndex;
        return controls.length ? controls : [root];
    }

    function initialFocus() {
        const root = rootFor(section);
        const items = candidates(root);
        return items.find(element => identity(element) === saved[section]) ||
            (section === 'followers' ? root.querySelector('.row-selected') : null) ||
            (section === 'commands' && window._currentView === 'dismissed' ?
                root.querySelector('[data-meridian-id$="-summon"]') : null) || items[0];
    }

    function activateSection(next, preferred) {
        if (!opened || !input || modalVisible()) return;
        if (preferred) saved[next] = preferred;
        stopScope?.();
        section = next;
        scopeRoot = rootFor(section);
        stopScope = input.attachNavigation({
            root: scopeRoot,
            initialFocus,
            getCandidates: () => candidates(scopeRoot),
            onBack: goBack,
            onAction: sectionAction
        });
        updateHints();
    }

    function goBack() {
        if (!opened) return;
        if (window._currentView === 'dismissed') {
            window.showGrid();
            activateSection('orders', 'btnDismissed');
        } else if (section !== 'followers') {
            activateSection('followers');
        } else {
            window.hordeRequestClose('controller');
        }
    }

    function sectionAction(event) {
        if (!opened) return true;
        if (event.phase !== 'press') return false;
        if (event.action === 'previousTab' || event.action === 'nextTab') {
            // Preserve the shared text-edit context if a text field is added later.
            if (document.activeElement?.matches('textarea,input:not([type=checkbox]):not([type=radio]):not([type=range])')) return true;
            const step = event.action === 'nextTab' ? 1 : -1;
            activateSection(sections[(sections.indexOf(section) + step + sections.length) % sections.length]);
            return true;
        }
        if (event.action === 'secondary' && window._currentView !== 'dismissed') {
            if (section === 'followers') activateSection('orders');
            else if (section === 'commands') activateSection('followers');
            else return false;
            return true;
        }
        if (event.action === 'tertiary' && section === 'followers' && !byId('btnDismissed').hidden &&
            byId('btnDismissed').getClientRects().length) {
            byId('btnDismissed').click();
            return true;
        }
        return false;
    }

    function updateHints() {
        const hints = byId('controllerHints');
        const state = input?.getState();
        const visible = !!(opened && state?.enabled && state.active && state.connected && gamepad());
        hints.hidden = !visible;
        document.documentElement.classList.toggle('horde-controller', visible);
        if (!visible) return;

        const title = document.createElement('strong');
        title.textContent = modalVisible() ? 'Confirmation' :
            section === 'commands' && window._currentView === 'dismissed' ? 'Dismissed' : labels[section];
        const items = [title];
        function prompt(action, description) {
            const binding = input.getPrompt(action);
            if (!binding.label) return;
            const item = document.createElement('span');
            const key = document.createElement('kbd');
            key.textContent = binding.label;
            item.append(key, ' ' + description);
            items.push(item);
        }
        prompt('accept', state.mode === 'cursor' ? 'Click' : section === 'followers' && !modalVisible() ? 'Select follower' : 'Select');
        prompt('cancel', modalVisible() ? 'Cancel' : section === 'followers' && window._currentView !== 'dismissed' ? 'Close' : 'Back');
        if (!modalVisible()) {
            prompt('previousTab', 'Previous section');
            prompt('nextTab', 'Next section');
            if (window._currentView !== 'dismissed') {
                if (section === 'followers') prompt('secondary', 'Group orders');
                else if (section === 'commands') prompt('secondary', 'Followers');
            }
            if (section === 'followers' && byId('btnDismissed').getClientRects().length) prompt('tertiary', 'Dismissed');
        }
        prompt('toggleCursor', state.mode === 'cursor' ? 'Navigation' : 'Cursor');
        hints.replaceChildren(...items);
    }

    function modalOpened() {
        if (!opened || !input) return;
        stopModal?.();
        stopModal = input.attachNavigation({
            root: byId('modalOverlay'), initialFocus: 'modalCancel',
            onBack: () => window.closeModal(),
            onAction: event => byId('modalOverlay').classList.contains('closing') ||
                ['previousTab', 'nextTab', 'secondary', 'tertiary'].includes(event.action)
        });
        updateHints();
    }

    function modalClosed() {
        stopModal?.();
        stopModal = null;
        if (opened && input && scopeRoot !== rootFor(section)) activateSection(section);
        updateHints();
    }

    function refresh() {
        if (!opened || !input) return;
        if (!modalVisible() && scopeRoot !== rootFor(section)) activateSection(section);
        // The shared helper's MutationObserver restores focus by stable ID after
        // each roster/detail refresh. Do not recreate the scope on every push.
        updateHints();
    }

    function initialize() {
        const candidate = window.MeridianInput;
        if (!candidate || candidate.version !== 1) return;
        input = candidate;
    }

    function close() {
        opened = false;
        stopModal?.(); stopModal = null;
        stopScope?.(); stopScope = null;
        stopState?.(); stopState = null;
        stopActions?.(); stopActions = null;
        scopeRoot = null;
        updateHints();
    }

    function open(formID) {
        close();
        initialize();
        opened = true;
        saved = formID ? { followers: 'follower-' + formID } : {};
        if (!input) return;
        stopState = input.onStateChange(updateHints);
        // Keep a closing modal on top until its animation ends. A repeated press
        // must never fall through to the underlying follower command.
        stopActions = input.onAction(() => !opened || byId('modalOverlay').classList.contains('closing'));
        activateSection('followers');
    }

    document.addEventListener('focusin', event => {
        if (opened && !modalVisible() && scopeRoot?.contains(event.target)) saved[section] = identity(event.target);
    });
    document.addEventListener('pointerdown', event => {
        if (!opened || !input || modalVisible()) return;
        const next = sections.find(name => rootFor(name).contains(event.target));
        if (next && next !== section) activateSection(next, identity(event.target.closest('button,input,[role="button"]')));
    }, true);
    window.addEventListener('pagehide', close);

    window.HordeController = Object.freeze({ initialize, open, close, refresh, modalOpened, modalClosed,
        screenChanged() {
            if (window._currentView === 'dismissed') activateSection('commands');
            else refresh();
        },
        followerSelected() {
            if (gamepad() && input.getState().mode === 'navigation') activateSection('commands');
        }
    });
    initialize();
})();
