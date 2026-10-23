// Краткие редакционные описания. Источники: materials/project-sources.json.
export const terminalProjects = [
  { id: 'puntopago', name: 'PUNTOPAGO', category: 'payments', context: 'Панама · сеть киосков', description: 'Выбор услуг, приём оплаты и серверная обработка операций в терминальной сети.', images: true },
  { id: 'trailquipt', name: 'Trailquipt', category: 'storage', context: 'Yellowstone · POS-vending', description: 'Самообслуживание для аренды, выдачи и возврата оборудования через locker-систему.' },
  { id: 'fincher', name: 'НКО ФИНЧЕР', category: 'payments', context: 'Самоинкассация', description: 'Внесение наличных на территории клиентов, учёт операций и мониторинг терминалов.' },
  { id: 'bank', name: 'Терминалы банка', category: 'payments', context: 'Оплата обучения', description: 'Сбор платежей за услуги вузов и передача транзакций в банковскую АБС.' },
  { id: 'xfit', name: 'X-fit', category: 'payments', context: 'Фитнес-клубы', description: 'Оплата и пополнение депозитов, RFID-браслеты и интеграция с клубной системой.' },
  { id: 'airport', name: 'Аэропорт Белгорода', category: 'payments', context: 'Оплата услуг', description: 'Приём оплаты, веб-мониторинг терминала и подготовка отчётности для 1С.' },
  { id: 'utility', name: 'Управляющие компании', category: 'payments', context: 'ЖК и ТСЖ', description: 'Приём платежей, связь с системой начислений и передача отчётов в 1С.' },
  { id: 'taxi', name: 'Таксопарки', category: 'payments', context: 'Счета водителей', description: 'Идентификация водителя, просмотр счёта и самостоятельное пополнение баланса.' },
  { id: 'provider', name: 'Городской оператор связи', category: 'payments', context: 'Терминальная сеть', description: 'Оплата услуг с интеграцией в биллинг и веб-доступом к мониторингу терминалов.' },
  { id: 'cashier', name: 'Электронный кассир', category: 'payments', context: 'Розничные точки', description: 'Автоматизация приёма оплаты и связь кассовых операций с системой учёта.' },
  { id: 'beauty', name: 'Салоны красоты', category: 'payments', context: 'Автоматизация оплаты', description: 'Внедрение электронных кассиров в сеть салонов.' },
  { id: 'courier', name: 'Инкассация для курьеров', category: 'payments', context: 'Сдача наличных', description: 'Терминальное решение для самостоятельной сдачи наличных курьерами.' },
  { id: 'rzd', name: 'Камеры хранения РЖД', category: 'storage', context: 'Платное хранение', description: 'Карты доступа, оплата хранения, информационные терминалы и управление ячейками.' },
  { id: 'ikea', name: 'Шкафчики для сотрудников ИКЕЯ', category: 'storage', context: 'Корпоративное хранение', description: 'Централизованный контроль доступа к шкафчикам по персональным RFID-картам.' },
  { id: 'leroy', name: 'Леруа Мерлен', category: 'storage', context: 'Хранение гаджетов', description: 'Система хранения и контроля использования гаджетов.' },
  { id: 'post', name: 'Почтомат для Почты РФ', category: 'storage', context: 'Пилотный проект', description: 'Программное решение пилотного проекта почтового автомата.' },
  { id: 'parcel', name: 'Система управления постаматами', category: 'storage', context: 'Распределённая сеть', description: 'Идентификация узлов, сбор транзакций и централизованный контроль состояния оборудования.' },
  { id: 'industrial', name: 'Промышленный вендинг', category: 'storage', context: 'Автоматизированная выдача', description: 'Программное обеспечение для систем промышленного вендинга.' },
];

export const controllers = [
  { name: 'SipLite', tag: 'СВЯЗЬ И ДОСТУП', description: 'Встраиваемый SIP-контроллер для домофонии, панелей доступа и IoT. ESP32 + STM32, Ethernet, NFC и RS-485.', href: 'https://siplite.ru', link: 'Описание контроллера' },
  { name: 'L4-HMI', tag: 'ИНТЕРФЕЙС УСТРОЙСТВА', image: 'hmi-panel', imageCaption: 'Панель L4-HMI · прототип', description: 'Развиваем проект HMI-интерфейсов на LVGL: каталоги, формы ввода и сценарии самообслуживания со связью через Leo4.', href: '/contacts/?topic=architecture', link: 'Обсудить применение' },
  { name: 'L4 Board IO-24', tag: 'ЗАМКИ И ДАТЧИКИ', image: 'io24-board', imageCaption: 'Плата Leo4 IO-24', description: 'Прошивка контроллера для постаматов и шкафов: 24 импульсных выхода, 24 цифровых входа, RS-485 / Modbus RTU.', href: 'https://github.com/OlegLebedevRU/l4-board-io-24-fw', link: 'Проект на GitHub' },
];
