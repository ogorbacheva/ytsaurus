(() => {
  const feedbackLabels = new Set(['Оставить отзыв', 'Leave feedback']);
  const desktopQuery = window.matchMedia('(min-width: 481px)');

  const positionFeedbackButton = () => {
    document.querySelectorAll('#dc-widgets button').forEach((button) => {
      if (!feedbackLabels.has(button.textContent.trim())) {
        return;
      }

      if (desktopQuery.matches) {
        button.style.setProperty('top', 'auto', 'important');
        button.style.setProperty('bottom', '280px', 'important');
      } else {
        button.style.removeProperty('top');
        button.style.removeProperty('bottom');
      }
    });
  };

  const observeWidgets = (widgets) => {
    positionFeedbackButton();
    new MutationObserver(positionFeedbackButton).observe(widgets, {
      childList: true,
      subtree: true,
    });
    desktopQuery.addEventListener('change', positionFeedbackButton);
  };

  const widgets = document.querySelector('#dc-widgets');
  if (widgets) {
    observeWidgets(widgets);
    return;
  }

  const pageObserver = new MutationObserver(() => {
    const mountedWidgets = document.querySelector('#dc-widgets');
    if (mountedWidgets) {
      pageObserver.disconnect();
      observeWidgets(mountedWidgets);
    }
  });

  pageObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });
})();
