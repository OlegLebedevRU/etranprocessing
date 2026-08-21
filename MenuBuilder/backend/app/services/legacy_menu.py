"""Import legacy menu tree and services into MenuBuilder for specified organizations."""

import asyncio
from typing import TypedDict

from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import async_session
from app.models import Group, MenuVariant, Service


class GroupDef(TypedDict):
    number: int
    name: str


class ServiceDef(TypedDict):
    tsp_code: int
    group_number: int
    name: str
    price: int
    printname: str | None
    protypenumber: int


ROOT_GROUP_NAME = "Раздел главного меню"
ROOT_GROUP_NUMBER = 0
LEGACY_VARIANT_NAME = "Легаси меню"

# 12 Categories under "Раздел главного меню"
LEGACY_CATEGORIES: list[GroupDef] = [
    {"number": 801, "name": "Стрижки"},
    {"number": 802, "name": "Укладка"},
    {"number": 803, "name": "Прическа"},
    {"number": 804, "name": "Брови"},
    {"number": 805, "name": "Сложное окрашивание"},
    {"number": 806, "name": "Окрашивание краской салона"},
    {"number": 807, "name": "Мелирование"},
    {"number": 808, "name": "Химическая завивка"},
    {"number": 809, "name": "Осветление"},
    {"number": 810, "name": "Смывка пигмента"},
    {"number": 811, "name": "Мытье головы"},
    {"number": 812, "name": "Комбо"},
]

# 49 Services
LEGACY_SERVICES: list[ServiceDef] = [
    {
        "tsp_code": 1000301,
        "group_number": 801,
        "name": "Мужская стрижка",
        "price": 600,
        "printname": "Стрижка стандартная",
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000302,
        "group_number": 801,
        "name": "Стрижка бороды",
        "price": 400,
        "printname": "Стрижка бороды",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000303,
        "group_number": 811,
        "name": "Сушка волос до 30 см",
        "price": 250,
        "printname": "Сушка волос до 30 см",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000304,
        "group_number": 801,
        "name": "Стрижка челки",
        "price": 250,
        "printname": "Стрижка челки",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000305,
        "group_number": 811,
        "name": "Мытье головы для мужчин",
        "price": 50,
        "printname": "Мытье головы для мужчин",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000306,
        "group_number": 805,
        "name": "Омбре",
        "price": 5450,
        "printname": "Окрашивание Омбре",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000307,
        "group_number": 801,
        "name": "Рисунок",
        "price": 350,
        "printname": None,
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000308,
        "group_number": 801,
        "name": "Женская стрижка",
        "price": 700,
        "printname": "Стрижка женская",
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000309,
        "group_number": 806,
        "name": "Короткие волосы (до 15 см)",
        "price": 1850,
        "printname": "Окрашивание — Короткие волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000310,
        "group_number": 805,
        "name": "Расчет стоимости",
        "price": 0,
        "printname": "Сложное окрашивание",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000311,
        "group_number": 806,
        "name": "Средние волосы (15-30 см)",
        "price": 2650,
        "printname": "Окрашивание — Средние волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000312,
        "group_number": 806,
        "name": "Длинные волосы (от 30 см)",
        "price": 3050,
        "printname": "Окрашивание — Длинные волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000313,
        "group_number": 807,
        "name": "Короткие волосы (до 15 см)",
        "price": 2800,
        "printname": "Мелирование — Короткие волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000314,
        "group_number": 808,
        "name": "Короткие волосы",
        "price": 2000,
        "printname": "Хим. завивка — Короткие волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000315,
        "group_number": 802,
        "name": "Короткие волосы",
        "price": 900,
        "printname": "Укладка — Короткие волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000316,
        "group_number": 802,
        "name": "Средние волосы",
        "price": 1300,
        "printname": "Укладка — Средние волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000317,
        "group_number": 802,
        "name": "Длинные волосы",
        "price": 1700,
        "printname": "Укладка — Длинные волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000318,
        "group_number": 803,
        "name": "Короткие волосы",
        "price": 1700,
        "printname": "Прическа — Короткие волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000319,
        "group_number": 803,
        "name": "Средние волосы",
        "price": 2300,
        "printname": "Прическа — Средние волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000320,
        "group_number": 803,
        "name": "Длинные волосы",
        "price": 2600,
        "printname": "Прическа — Длинные волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000321,
        "group_number": 804,
        "name": "Оформление бровей",
        "price": 350,
        "printname": "Оформление бровей",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000322,
        "group_number": 804,
        "name": "Окрашивание бровей",
        "price": 350,
        "printname": "Окрашивание бровей",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000323,
        "group_number": 804,
        "name": "Оформление и окрашивание бровей",
        "price": 700,
        "printname": "Оформление и окрашивание бровей",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000324,
        "group_number": 808,
        "name": "Средние волосы",
        "price": 2700,
        "printname": "Хим. завивка — Средние волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000325,
        "group_number": 808,
        "name": "Длинные волосы",
        "price": 3200,
        "printname": "Хим. завивка — Длинные волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000326,
        "group_number": 807,
        "name": "Средние волосы (15-30 см)",
        "price": 3500,
        "printname": "Мелирование — Средние волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000327,
        "group_number": 807,
        "name": "Длинные волосы (от 30 см)",
        "price": 4000,
        "printname": "Мелирование — Длинные волосы",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000328,
        "group_number": 812,
        "name": "Стрижка Фейд + мытье головы",
        "price": 650,
        "printname": "Фейд + мытье",
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000329,
        "group_number": 812,
        "name": "Акция 1+1 (мужские)",
        "price": 1150,
        "printname": "Акция 1+1 (мужские)",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000330,
        "group_number": 812,
        "name": "Стрижка + Борода + Мытье",
        "price": 1050,
        "printname": "Стрижка + Борода + Мытье",
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000331,
        "group_number": 806,
        "name": "Расчет красителя (10гр-50руб)",
        "price": 0,
        "printname": "Расчет красителя",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000332,
        "group_number": 812,
        "name": "Стрижка мужская + мытье головы",
        "price": 650,
        "printname": "Стрижка мужская + мытье головы",
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000333,
        "group_number": 801,
        "name": "Снятие сечки",
        "price": 900,
        "printname": "Снятие сечки",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000334,
        "group_number": 803,
        "name": "Прическа плюс",
        "price": 0,
        "printname": "Прическа плюс",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000335,
        "group_number": 807,
        "name": "Расчет пудры (10гр-50руб)",
        "price": 0,
        "printname": "Расчет пудры",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000336,
        "group_number": 807,
        "name": "Тонирование волос",
        "price": 1300,
        "printname": "Тонирование волос",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000338,
        "group_number": 807,
        "name": "Экспресс-тонирование",
        "price": 550,
        "printname": "Экспресс-тонирование",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000339,
        "group_number": 808,
        "name": "Химическая завивка плюс",
        "price": 0,
        "printname": "Хим. завивка — Индивидуальный расчет",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000341,
        "group_number": 811,
        "name": "Мытье головы для женщин",
        "price": 150,
        "printname": "Мытье головы для женщин",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000342,
        "group_number": 811,
        "name": "Мытье головы для женщин (свыше 30 см)",
        "price": 200,
        "printname": "Мытье головы для женщин — свыше 30 см",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000343,
        "group_number": 801,
        "name": "Стрижка Фейд",
        "price": 650,
        "printname": None,
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000344,
        "group_number": 801,
        "name": "Стрижка кончиков",
        "price": 550,
        "printname": "Стрижка кончиков",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000345,
        "group_number": 801,
        "name": "Стрижка детская",
        "price": 600,
        "printname": "Стрижка детская",
        "protypenumber": 990021,
    },
    {
        "tsp_code": 1000346,
        "group_number": 801,
        "name": "Стрижка под одну насадку",
        "price": 400,
        "printname": "Стрижка под одну насадку",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000350,
        "group_number": 809,
        "name": "Осветление экстра",
        "price": 0,
        "printname": "Осветление — Индивидуальный расчет",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000351,
        "group_number": 810,
        "name": "Смывка пигмента плюс",
        "price": 0,
        "printname": "Смывка пигмента — Индивидуальный расчет",
        "protypenumber": 99002,
    },
    {
        "tsp_code": 1000352,
        "group_number": 801,
        "name": "Стрижка пенсионная",
        "price": 450,
        "printname": "Стрижка пенсионная",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000357,
        "group_number": 805,
        "name": "Шатуш",
        "price": 5450,
        "printname": "Окрашивание Шатуш",
        "protypenumber": 99001,
    },
    {
        "tsp_code": 1000361,
        "group_number": 805,
        "name": "Airtouch",
        "price": 5450,
        "printname": "Окрашивание Airtouch",
        "protypenumber": 99001,
    },
]


async def import_legacy_menu_variant(
    session: AsyncSession,
    org_id: int,
    variant_name: str = LEGACY_VARIANT_NAME,
) -> MenuVariant:
    """Import or overwrite legacy menu variant, group tree, and services for a given organization."""
    # 1. Find or create menu variant
    stmt = select(MenuVariant).where(
        MenuVariant.org_id == org_id, MenuVariant.name == variant_name
    )
    res = await session.execute(stmt)
    variant = res.scalar_one_or_none()

    if variant:
        # Delete existing services and groups for this variant
        await session.execute(
            delete(Service).where(Service.menu_variant_id == variant.id)
        )
        await session.execute(delete(Group).where(Group.menu_variant_id == variant.id))
        await session.flush()
    else:
        variant = MenuVariant(org_id=org_id, name=variant_name)
        session.add(variant)
        await session.flush()

    # 2. Create root group ("Раздел главного меню", number=0, parent_id=None)
    root_group = Group(
        menu_variant_id=variant.id,
        org_id=org_id,
        number=ROOT_GROUP_NUMBER,
        name=ROOT_GROUP_NAME,
        parent_id=None,
    )
    session.add(root_group)
    await session.flush()

    # 3. Create 12 categories under root_group
    groups_by_number: dict[int, Group] = {}
    for cat in LEGACY_CATEGORIES:
        grp = Group(
            menu_variant_id=variant.id,
            org_id=org_id,
            number=cat["number"],
            name=cat["name"],
            parent_id=root_group.id,
        )
        session.add(grp)
        groups_by_number[cat["number"]] = grp
    await session.flush()

    # 4. Create 49 services
    for s_def in LEGACY_SERVICES:
        grp = groups_by_number.get(s_def["group_number"])
        if not grp:
            raise ValueError(
                f"Group number {s_def['group_number']} not found for service {s_def['tsp_code']}"
            )
        svc = Service(
            menu_variant_id=variant.id,
            group_id=grp.id,
            tsp_code=s_def["tsp_code"],
            name=s_def["name"],
            printname=s_def["printname"],
            price=s_def["price"],
            protypenumber=s_def["protypenumber"],
        )
        session.add(svc)

    await session.flush()
    return variant


async def import_legacy_menus_for_orgs(
    org_ids: list[int] | None = None,
    variant_name: str = LEGACY_VARIANT_NAME,
) -> dict[int, int]:
    """Import legacy menu for all specified orgs and commit."""
    if org_ids is None:
        org_ids = [424, 1]
    result: dict[int, int] = {}
    async with async_session() as session, session.begin():
        for org_id in org_ids:
            variant = await import_legacy_menu_variant(
                session, org_id=org_id, variant_name=variant_name
            )
            result[org_id] = variant.id
    return result


async def main():
    print("Starting import of legacy menus for orgs: 424, 1...")
    res = await import_legacy_menus_for_orgs([424, 1])
    print(f"Successfully imported legacy menus: {res}")


if __name__ == "__main__":
    asyncio.run(main())
