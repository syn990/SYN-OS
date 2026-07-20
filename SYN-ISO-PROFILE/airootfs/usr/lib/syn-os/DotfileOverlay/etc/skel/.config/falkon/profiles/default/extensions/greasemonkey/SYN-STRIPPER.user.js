// ==UserScript==
// @name         SYN YouTube Ad DOM OBJECT SPEEDERUPPER
// @description  Skip and accelerate YouTube ads - based on live DOM analysis
// @author       syntax990
// @match        *://*.youtube.com/*
// @exclude      *://*.youtube.com/subscribe_embed?*
// @run-at       document-idle
// @grant        none
// ==/UserScript==

(function () {
    'use strict';

    const COOLDOWN_KEY = 'syn_yt_cooldown';
    const COOLDOWN_MS = 10_000; // sit out 10s after reload before re-arming

    const SELECTORS = {
        skip: [
            '.ytp-skip-ad-button',
            '.ytp-ad-skip-button',
            '.ytp-ad-skip-button-modern',
            '.videoAdUiSkipButton',
        ].join(','),
        adShowing: '.ad-showing',
        video: 'video',
        enforcement: [
            'ytd-enforcement-message-view-model',
            '.yt-mealbar-promo-renderer',
        ].join(','),
    };

    // --- Reload-and-sit-out logic ---
    const coolingDown = () => {
        const until = parseInt(sessionStorage.getItem(COOLDOWN_KEY) || '0', 10);
        return Date.now() < until;
    };

    const triggerReload = () => {
        // Set cooldown BEFORE reload so the reloaded page sees it immediately
        sessionStorage.setItem(COOLDOWN_KEY, Date.now() + COOLDOWN_MS);
        window.location.reload();
    };

    // If we just reloaded to escape the banner, wait before re-arming
    if (coolingDown()) {
        const remaining = parseInt(sessionStorage.getItem(COOLDOWN_KEY), 10) - Date.now();
        console.log(`[SYN] Cooling down for ${Math.ceil(remaining / 1000)}s`);
        setTimeout(() => {
            sessionStorage.removeItem(COOLDOWN_KEY);
            arm(); // re-arm after cooldown
        }, remaining);
        return; // don't arm immediately
    }

    // --- Core ad handler ---
    const handle = () => {
        // Enforcement banner detected — bail and reload clean
        if (document.querySelector(SELECTORS.enforcement)) {
            observer.disconnect();
            clearInterval(poll);
            triggerReload();
            return;
        }

        const btn = document.querySelector(SELECTORS.skip);
        if (btn) btn.click();

        if (document.querySelector(SELECTORS.adShowing)) {
            const v = document.querySelector('.ad-showing video')
                   ?? document.querySelector(SELECTORS.video);
            if (v) {
                v.muted = true;
                v.playbackRate = 10;
                if (v.duration && isFinite(v.duration)) {
                    v.currentTime = v.duration - 0.01;
                }
            }
        }
    };

    // --- Observer + poll wrapped so arm() can restart them ---
    let observer, poll;

    const arm = () => {
        let rafPending = false;
        const debouncedHandle = () => {
            if (rafPending) return;
            rafPending = true;
            requestAnimationFrame(() => { handle(); rafPending = false; });
        };

        observer = new MutationObserver(debouncedHandle);
        observer.observe(document.body, {
            childList: true,
            subtree: true,
            attributes: true,
            attributeFilter: ['class'],
        });

        poll = setInterval(handle, 500);
    };

    arm();

})();
