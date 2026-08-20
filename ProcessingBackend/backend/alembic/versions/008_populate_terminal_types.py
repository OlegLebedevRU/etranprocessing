"""Populate full terminal types dictionary (0..144).

Revision ID: 008
Revises: 007
Create Date: 2026-08-20
"""

from collections.abc import Sequence

import sqlalchemy as sa

from alembic import op

revision: str = "008"
down_revision: str | None = "007"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

TERMINAL_TYPES_DATA = [
    (0, "Empty", "начальная настройка"),
    (1, "PaymentTerminal", "Платежный терминал"),
    (2, "Cashier", "Касса"),
    (3, "Vending", "Вендинг"),
    (4, "ATM", "Банкомат"),
    (5, "Postomat", "Постомат"),
]

# 6-73 Platerra Терминал
for i in range(6, 74):
    TERMINAL_TYPES_DATA.append((i, f"PlaterraTerminal_{i}", "Platerra Терминал"))

SPECIFIC_TYPES = [
    (74, "StorageBox", "Камера хранения"),
    (78, "SafeBox", "Главное окно сейфа"),
    (79, "RentPoint", "Пункт проката"),
    (80, "WBPost", "Постомат Вайлдберри"),
    (82, "SBCashier", "Касса АКХ"),
    (85, "KeysKeeper", "Хранение ключей"),
    (86, "PostPaymentAKH", "АКХ с пост-оплатой"),
    (87, "ShiftAccessSB", "Камера хранения с посменным доступом"),
    (89, "EasySB", "Камера хранения без интерфейса с удалённой кассой"),
    (90, "AKHInfoKiosk", "Инфокиоск АКХ"),
    (91, "QueueBox", "Сейф без задней стенки"),
    (92, "VendingMashine", "Торговый автомат"),
    (93, "Evolution", "АКХ EVOLUTION"),
    (94, "EvolutionInfo", "Инфокиоск EVOLUTION"),
    (95, "AKH_RFID", "АКХ RFID"),
    (96, "Concierge", "Concierge"),
    (97, "Depo", "Depo"),
    (98, "OhtaPark", "Охта-парк"),
    (99, "Laundry", "Прачечная"),
    (100, "SBHybrid", "ЭС с гибридным доступом"),
    (101, "CoworkingKeys", "Ключница коворкинга"),
    (102, "AquaPark", "АКХ Аквапарк"),
    (103, "DWK", "Камера хранения ДВК"),
    (104, "ASS", "АСХ комплектующих"),
    (105, "EasyPost", "Простой сетевой постомат"),
    (106, "PPE", "Шкафы СИЗ"),
    (107, "KeyBox", "Ключница"),
    (108, "Leroy", "Хранитель сканеров Леруа"),
    (109, "VKH", "Вендинговая камера хранения"),
    (110, "MUBitrix", "Миграционный учёт. Битрикс"),
    (111, "DCTStorage", "Хранение терминалов сбора данных"),
    (112, "Trailquipt", "Trailquipt"),
    (113, "FMLogistic", "FMLogistic"),
    (114, "APoint", "APoint"),
    (115, "OfficeBox", "OfficeBox"),
    (116, "ApartmentKeys", "ApartmentKeys"),
    (117, "Chairs", "Chairs"),
    (118, "CoffeeMania", "CoffeeMania"),
    (119, "LentaKeys", "LentaKeys"),
    (120, "NestleEquipment", "NestleEquipment"),
    (121, "Pepsico", "Pepsico"),
    (122, "RemoteCells", "RemoteCells"),
    (123, "FingerStorage", "FingerStorage"),
    (124, "PostBox", "PostBox"),
    (125, "ServiceBasket", "ServiceBasket"),
    (126, "ESClient", "ESClient"),
    (127, "ESServer", "ESServer"),
    (128, "AutoKeysKeeper", "AutoKeysKeeper"),
    (129, "SberKeyBox", "SberKeyBox"),
    (130, "SberKeyBoxAdmin", "SberKeyBoxAdmin"),
    (131, "BGPost", "BGPost"),
    (132, "ZKCons", "ZKCons"),
    (133, "Keysomat", "Keysomat"),
    (134, "SIZRenter", "SIZRenter"),
    (135, "PresidentTaxi", "PresidentTaxi"),
    (136, "Lamoda", "Lamoda"),
    (137, "LamodaServer", "LamodaServer"),
    (138, "InOutCashier", "InOutCashier"),
    (139, "VendingWithOrders", "VendingWithOrders"),
    (140, "Polet", "Polet"),
    (141, "FitnessEquipment", "FitnessEquipment"),
    (142, "AKH", "Главная форма AKH"),
    (143, "QRStorages", "QRStorages"),
    (144, "TechnoAvia", "TechnoAvia"),
]
TERMINAL_TYPES_DATA.extend(SPECIFIC_TYPES)


def upgrade() -> None:
    # Use UPSERT on conflict
    for type_id, name, desc in TERMINAL_TYPES_DATA:
        op.execute(
            sa.text(
                "INSERT INTO terminal_types (id, name, description) "
                "VALUES (:id, :name, :description) "
                "ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name, description = EXCLUDED.description"
            ).bindparams(id=type_id, name=name, description=desc)
        )


def downgrade() -> None:
    # Remove added types > 5
    op.execute(sa.text("DELETE FROM terminal_types WHERE id > 5"))
