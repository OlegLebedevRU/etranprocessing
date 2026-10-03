# Архив прежней финансовой модели L4Desk

Расчёты баланса, минут, monthly online, posting и tenant-wide entitlement здесь
заморожены. Они сохраняют возможность чтения истории и проверки прежних расчётов,
но не являются активным runtime биллингом. Не подключать их workers или mutation
API к новому startup.

Активные подписки находятся в `../subscriptions.py`, `../subscription_payments.py`
и `../subscription_worker.py`. Техническая длительность — `../usage.py` и
`../remote_session_metering.py`; остановка — `../remote_session_stop.py`.
`yookassa.py` и `stop_outbox.py` оставлены только как compatibility exports.

Правила перехода: [подписки L4Desk](../../../../../docs/menu_bill-terminal-subscription.md).
