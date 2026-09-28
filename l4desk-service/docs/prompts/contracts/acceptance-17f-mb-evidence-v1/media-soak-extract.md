3. 120-минутный media soak терминала `1000003` выполнен 2026-09-26 через
   обычный сайт под тестовым owner tenant 3. Сессия БД `462` шла
   18:01:26–20:06:18 UTC (7492 с, 2:04:52); перед stop ingress показывал
   `fresh_rtp=true`, `rtp_idle_sec=0`, маршрут и 0 unrouted packets.
   Владелец подтвердил живое видео без задержки и штатный stop. После stop
   сессия закрыта, route и затем transport исчезли. **Media soak: PASS**.
   `fin_usage_daily.id=1` после stop содержит 9244 с за день, включая
   предыдущие сессии: 7200 free, 2044 billable, рассчитано 100 копеек.
   Строка ещё открыта (`ledger_transaction_id`/`posted_at` NULL), поэтому
   баланс 1000 копеек не изменился. Это evidence измерения usage, не
   подтверждение окончательного списания или блокировки.
   Контрольный повторный запуск после этого stop создал сессию `465`:
   20:12:45–20:13:47 UTC (61 с). Пользователь подтвердил быстрый старт,
   движение без задержки и остановку без 500. `fin_usage_daily.id=1`
   вырос ровно на 61 с до 9305 source / 9279 video / 2105 billable;
   повторного начисления 120-минутной сессии нет. После stop ingress
   показывает `state=disconnected`, `transport_connected=false` и
   `route_exists=false`. **Повторный start/stop: PASS**.
4. Изолированный install/upgrade/rollback релизного `l4setup` и x86 runtime
   на Windows 7/POSReady 7 остаются за рамками подтверждённого теста.
