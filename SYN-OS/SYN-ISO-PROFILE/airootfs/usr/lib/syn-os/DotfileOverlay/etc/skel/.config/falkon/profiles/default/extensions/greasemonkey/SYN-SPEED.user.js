// ==UserScript== 
// @name        SYN-SPEED 
// @namespace   kde.org 
// @description Script description 
// @include     * 
// @version     1.0.0 
// ==/UserScript==

window.addEventListener('keydown', (e) => {
    // metaKey is the 'Super' / 'Windows' key
    if (e.metaKey) {
        const video = document.querySelector('video');
        if (!video) return;

        // Super + Comma (Speed Down)
        if (e.key === ',') {
            e.preventDefault();
            video.playbackRate = Math.max(0.25, video.playbackRate - 0.5);
            console.log("SYN-OS: Speed decreased to " + video.playbackRate);
        }

        // Super + Period (Speed Up)
        if (e.key === '.') {
            e.preventDefault();
            video.playbackRate = Math.min(16, video.playbackRate + 0.5);
            console.log("SYN-OS: Speed increased to " + video.playbackRate);
        }
        
        // Bonus: Super + / (Reset to 1x)
        if (e.key === '/') {
            e.preventDefault();
            video.playbackRate = 1.0;
        }
    }
}, true);
