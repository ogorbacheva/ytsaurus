(() => {
    'use strict';

    const installation = Symbol.for('ytsaurus.landing.search');
    if (window[installation]) return;
    window[installation] = true;

    function mountSearch() {
        const input = document.getElementById('yt-landing-search');
        if (!input || input.dataset.searchReady === 'true') return;

        input.addEventListener('keydown', (event) => {
            if (event.key !== 'Enter' || event.isComposing || event.repeat) return;
            event.preventDefault();
            const query = input.value.trim();
            if (!query) return;

            // Use the same results page as the Viewer's header search.
            // Resolve from the landing URL to preserve a proxy prefix, if any.
            const current = new URL(window.location.href);
            const target = new URL('./docs-viewer/search', current);
            target.searchParams.set('query', query);
            const revision = current.searchParams.get('revision');
            const version = current.searchParams.get('version');
            if (revision) {
                target.searchParams.set('revision', revision);
            } else if (version) {
                target.searchParams.set('version', version);
            }
            window.location.assign(target.href);
        });

        input.disabled = false;
        input.enterKeyHint = 'search';
        input.dataset.searchReady = 'true';
    }

    // The Viewer can replace article content during client-side navigation.
    new MutationObserver(mountSearch).observe(document.documentElement, {
        childList: true,
        subtree: true,
    });
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', mountSearch, {once: true});
    }
    mountSearch();
})();
