(() => {
  document.documentElement.classList.add('js');
  document.querySelectorAll('.project-images, .prototype-media').forEach(details => {
    details.addEventListener('toggle', () => {
      if (!details.open) return;
      details.querySelectorAll('img[data-src]').forEach(img => {
        img.src = img.dataset.src;
        delete img.dataset.src;
      });
    });
  });
  const filters = document.querySelectorAll('[data-project-filter]');
  const projectRows = document.querySelectorAll('[data-project-category]');
  filters.forEach(button => button.addEventListener('click', () => {
    const selected = button.dataset.projectFilter;
    filters.forEach(filter => filter.setAttribute('aria-pressed', String(filter === button)));
    let visible = 0;
    projectRows.forEach(row => {
      row.hidden = selected !== 'all' && row.dataset.projectCategory !== selected;
      if (!row.hidden) visible += 1;
    });
    const status = document.querySelector('.project-status');
    if (status) status.textContent = `Показано проектов: ${visible}`;
  }));
  const toggle = document.querySelector('.menu-toggle');
  const nav = document.querySelector('#navigation');
  const closeMenu = () => {
    nav?.classList.remove('is-open');
    toggle?.setAttribute('aria-expanded', 'false');
    toggle?.setAttribute('aria-label', 'Открыть меню');
  };
  toggle?.addEventListener('click', () => {
    const open = nav.classList.toggle('is-open');
    toggle.setAttribute('aria-expanded', String(open));
    toggle.setAttribute('aria-label', open ? 'Закрыть меню' : 'Открыть меню');
  });
  nav?.querySelectorAll('a').forEach(a => a.addEventListener('click', closeMenu));
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape' && nav?.classList.contains('is-open')) {
      closeMenu(); toggle.focus();
    }
  });
  const dialog = document.querySelector('.image-dialog');
  let trigger;
  if (dialog && typeof dialog.showModal === 'function') {
    document.querySelectorAll('[data-image]').forEach(a => a.addEventListener('click', e => {
      if (e.ctrlKey || e.metaKey || e.shiftKey || e.altKey || e.button !== 0) return;
      e.preventDefault(); trigger = a;
      const img = dialog.querySelector('img');
      img.src = a.href; img.alt = a.dataset.caption;
      dialog.querySelector('p').textContent = a.dataset.caption;
      dialog.showModal(); document.body.style.overflow = 'hidden';
    }));
    dialog.querySelector('.dialog-close').addEventListener('click', () => dialog.close());
    dialog.addEventListener('click', e => { if (e.target === dialog) { const r = dialog.getBoundingClientRect(); if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) dialog.close(); } });
    dialog.addEventListener('close', () => { document.body.style.overflow = ''; trigger?.focus(); });
  }
  const select = document.querySelector('#project-topic');
  const compose = document.querySelector('#compose-email');
  if (select && compose) {
    const names = { general: 'Проект для Platerra', l4desk: 'Внедрение L4Desk', leo4: 'Интеграция Leo4 IoT Platform', terminal: 'PlaterraTerminal', architecture: 'Распределённая архитектура' };
    const topic = new URLSearchParams(location.search).get('topic');
    if (Object.hasOwn(names, topic)) select.value = topic;
    const update = () => {
      const body = 'Здравствуйте!\n\nХочу обсудить: ' + names[select.value] + '.\n\nУстройства и приложения:\n\nПользователи системы:\n\nНужный результат:\n\nМои контакты:\n';
      compose.href = 'mailto:info@platerra.ru?subject=' + encodeURIComponent(names[select.value]) + '&body=' + encodeURIComponent(body);
    };
    select.addEventListener('change', update); update();
  }
})();
