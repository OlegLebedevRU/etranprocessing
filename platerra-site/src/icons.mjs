// Небольшие предметные иллюстрации: собственный SVG, без библиотек и запросов.
const shapes = {
  puntopago: '<rect x="15" y="7" width="20" height="33" rx="3" fill="#f4f5ef"/><rect x="19" y="12" width="12" height="11" rx="1" fill="#cbf478"/><path d="M20 28h10M22 33h6M12 42h26"/>',
  trailquipt: '<rect x="8" y="11" width="23" height="30" rx="2" fill="#f4f5ef"/><path d="M8 21h23M8 31h23M25 15v2M25 25v2M25 35v2"/><rect x="33" y="16" width="8" height="20" rx="2" fill="#cbf478"/><path d="M36 20h2"/>',
  fincher: '<rect x="10" y="11" width="30" height="29" rx="3" fill="#f4f5ef"/><rect x="15" y="17" width="20" height="6" rx="1" fill="#cbf478"/><path d="M15 28h20M25 30v7m-4-4 4 4 4-4"/>',
  bank: '<path d="m7 18 18-10 18 10Z" fill="#cbf478"/><path d="M11 22v14M20 22v14M30 22v14M39 22v14M8 40h34M7 44h36"/>',
  xfit: '<path d="M16 17v16M12 20v10M34 17v16M38 20v10M16 25h18"/><rect x="8" y="22" width="4" height="6" rx="1" fill="#cbf478"/><rect x="38" y="22" width="4" height="6" rx="1" fill="#cbf478"/>',
  airport: '<path d="m24 7 4 16 13 9v4l-13-5-1 10h-4l-1-10-13 5v-4l13-9 1-16Z" fill="#cbf478"/>',
  utility: '<path d="M10 41V16l12-7v32M22 41V21l16-6v26M7 41h35M15 19v3M15 28v3M28 24v3M34 22v3M28 33v3M34 31v3"/><path d="M22 41V21l16-6v26Z" fill="#cbf478"/><path d="M28 24v3M34 22v3M28 33v3M34 31v3"/>',
  taxi: '<path d="m10 24 5-10h20l5 10v13H10Z" fill="#f4f5ef"/><path d="M10 24h30M17 30h3M30 30h3M15 37v4M35 37v4"/><rect x="21" y="9" width="8" height="5" rx="1" fill="#cbf478"/>',
  provider: '<path d="M25 20v21M18 41h14M17 26a11 11 0 0 1 0-16M33 10a11 11 0 0 1 0 16M12 31a18 18 0 0 1 0-26M38 5a18 18 0 0 1 0 26"/><circle cx="25" cy="18" r="4" fill="#cbf478"/>',
  cashier: '<rect x="10" y="29" width="30" height="11" rx="2" fill="#cbf478"/><path d="M14 29V18h22v11M25 18V9M18 9h14v9M17 34h5M32 34h2"/>',
  beauty: '<circle cx="14" cy="35" r="5" fill="#cbf478"/><circle cx="31" cy="35" r="5" fill="#cbf478"/><path d="m17 31 18-20M28 31 11 10M21 24l5 5"/>',
  courier: '<path d="M8 23h22v15H8Z" fill="#cbf478"/><path d="M30 28h7l6 6v4H30M8 18h16M5 13h15"/><circle cx="16" cy="39" r="4" fill="#f4f5ef"/><circle cx="36" cy="39" r="4" fill="#f4f5ef"/>',
  rzd: '<rect x="12" y="9" width="26" height="28" rx="5" fill="#f4f5ef"/><rect x="17" y="15" width="16" height="9" rx="1" fill="#cbf478"/><path d="M17 30h1M32 30h1M17 37l-5 6M33 37l5 6M14 41h22"/>',
  ikea: '<rect x="10" y="9" width="30" height="32" rx="2" fill="#f4f5ef"/><path d="M25 9v32M20 18v8M30 18v8"/><rect x="8" y="32" width="13" height="9" rx="2" fill="#cbf478"/><path d="M11 36h4"/>',
  leroy: '<rect x="9" y="10" width="32" height="31" rx="2" fill="#f4f5ef"/><path d="M9 25h32M25 10v31"/><rect x="14" y="14" width="6" height="8" rx="1" fill="#cbf478"/><rect x="30" y="29" width="6" height="8" rx="1" fill="#cbf478"/>',
  post: '<rect x="10" y="9" width="30" height="33" rx="2" fill="#f4f5ef"/><path d="M10 20h30M10 31h30M25 9v33"/><rect x="28" y="11" width="9" height="7" rx="1" fill="#cbf478"/><path d="m28 12 4 3 5-3M20 24v3M30 35v3"/>',
  parcel: '<path d="m11 17 14-8 14 8v17l-14 8-14-8Z" fill="#cbf478"/><path d="m11 17 14 8 14-8M25 25v17M18 13l14 8M31 27l4-2"/>',
  industrial: '<rect x="11" y="8" width="28" height="34" rx="2" fill="#f4f5ef"/><path d="M11 19h28M11 30h28M29 8v34M34 13v2M34 24v2M34 35v2"/><path d="m16 24 4-3 4 3-4 3Z" fill="#cbf478"/>',
};

export function projectIcon(id) {
  return `<svg viewBox="0 0 50 50" width="50" height="50" aria-hidden="true" focusable="false" fill="none" stroke="#405139" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">${shapes[id] || shapes.parcel}</svg>`;
}
