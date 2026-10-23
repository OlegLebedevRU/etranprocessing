import { mkdir, writeFile, copyFile, cp } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { products } from './src/content.mjs';
import { layout, home, productPage, expertise, contacts } from './src/templates.mjs';

const root = fileURLToPath(new URL('.', import.meta.url));
const out = resolve(root, 'dist');
await mkdir(resolve(out, 'assets'), { recursive: true });
await Promise.all(['styles.css', 'main.js'].map(name => copyFile(resolve(root, 'src', name), resolve(out, 'assets', name))));
await cp(resolve(root, 'assets'), resolve(out, 'assets'), { recursive: true });
const pages = [
  { path: '/', title: 'Platerra — от устройства до облака', description: 'L4Desk, Leo4 IoT Platform и PlaterraTerminal. Создаём продукты и распределённые системы: устройства, серверные сервисы и интерфейсы управления.', body: home() },
  ...products.map(p => ({ path: p.path, title: `${p.name} — ${p.short} | Platerra`, description: p.description, body: productPage(p) })),
  { path: '/expertise/', title: 'Распределённые приложения и инженерная разработка | Platerra', description: 'Архитектура, устройства, API, события и интерфейсы управления. Разработка и развитие распределённых приложений с Platerra.', body: expertise() },
  { path: '/contacts/', title: 'Обсудить проект | Platerra', description: 'Расскажите о своей задаче: удалённое управление, IoT, терминалы самообслуживания или распределённая система. info@platerra.ru.', body: contacts() },
];
for (const p of pages) {
  const dir = resolve(out, `.${p.path}`);
  await mkdir(dir, { recursive: true });
  await writeFile(resolve(dir, 'index.html'), layout(p), 'utf8');
}
await writeFile(resolve(out, '404.html'), layout({path:'/404.html',title:'Страница не найдена | Platerra',description:'Вернитесь к решениям Platerra.',body:'<section class="shell contact-page"><p class="eyebrow">404 / СТРАНИЦА НЕ НАЙДЕНА</p><h1>Эта связь<br>не найдена.</h1><p class="hero-lead">Возможно, адрес изменился. Начните с решений Platerra.</p><div class="hero-actions"><a class="button button-dark" href="/">На главную <span aria-hidden="true">↗</span></a><a class="text-link" href="/contacts/">Связаться с нами</a></div></section>'}), 'utf8');
await writeFile(resolve(out, 'robots.txt'), 'User-agent: *\nAllow: /\nSitemap: https://platerra.ru/sitemap.xml\n');
await writeFile(resolve(out, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${pages.map(p=>`<url><loc>https://platerra.ru${p.path}</loc></url>`).join('')}</urlset>`);
console.log(`Built ${pages.length} pages in ${out}; no runtime framework or external dependencies.`);
