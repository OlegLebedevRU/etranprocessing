"""Payment flow service - handles business logic for payment processing."""

import base64
import hashlib
from datetime import UTC, datetime

from sqlalchemy import and_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import (
    BalanceTerminalTsp,
    MenuVariantSnapshot,
    Payment,
    PaymentParam,
    ServiceMenu,
    Terminal,
    TerminalMenuBinding,
    Tsp,
    TspParameterCode,
)


class PaymentService:
    """Service for handling payment flow operations."""

    def __init__(self, db: AsyncSession):
        self.db = db

    async def get_tsp_by_code(self, tsp_code: int) -> Tsp | None:
        """Get TSP by tsp_code."""
        result = await self.db.execute(select(Tsp).where(Tsp.tsp_code == tsp_code))
        return result.scalar_one_or_none()

    async def get_prototypenumber_by_tsp_code(self, tsp_code: int) -> int:
        """Get prototypenumber from services table by tsp_code. Default 99000 if not found."""
        result = await self.db.execute(
            select(ServiceMenu.protypenumber)
            .where(ServiceMenu.tsp_code == tsp_code)
            .limit(1)
        )
        row = result.scalar_one_or_none()
        return row if row is not None else 99000

    async def get_param_id(
        self, prototypenumber: int, parameter_code: int
    ) -> int | None:
        """Get param_id from tsp_parameter_codes by prototypenumber and parameter_code."""
        result = await self.db.execute(
            select(TspParameterCode.param_id).where(
                and_(
                    TspParameterCode.prototypenumber == prototypenumber,
                    TspParameterCode.parameter_code == parameter_code,
                )
            )
        )
        return result.scalar_one_or_none()

    async def create_param_code(self, prototypenumber: int, parameter_code: int) -> int:
        """Create new parameter code if not exists (like legacy AModule_AddPaymentParam)."""
        param = TspParameterCode(
            prototypenumber=prototypenumber,
            parameter_code=parameter_code,
            code_description=f"Рекв{parameter_code}",
        )
        self.db.add(param)
        await self.db.flush()
        return param.param_id

    async def get_payment_by_ext_id(self, paym_ext_id: str) -> Payment | None:
        """Get existing payment by external ID (for idempotency)."""
        result = await self.db.execute(
            select(Payment).where(Payment.paym_ext_id == paym_ext_id)
        )
        return result.scalar_one_or_none()

    async def create_payment(
        self,
        terminal: Terminal,
        tsp_code: int,
        amount: int,
        paym_ext_id: str,
        params: dict[int, str],
        pay_type_id: int = 0,
    ) -> Payment:
        """
        Create payment with params and update balance.
        Idempotent: if payment with same paym_ext_id exists, return it.

        Args:
            terminal: Terminal object from auth
            tsp_code: TSP code (PaymSubjTp)
            amount: Amount in kopeks
            paym_ext_id: External payment ID from terminal
            params: Dict of {parameter_code: param_value}
            pay_type_id: Payment type (1=cash, 2=card, 3=SBP, 4=combo, ...)

        Returns:
            Created or existing Payment object
        """
        # Idempotency check
        existing = await self.get_payment_by_ext_id(paym_ext_id)
        if existing:
            return existing

        # Get TSP
        tsp = await self.get_tsp_by_code(tsp_code)
        if not tsp:
            raise ValueError(f"TSP not found for code {tsp_code}")

        # Get prototypenumber for parameter lookup (default 99000)
        prototypenumber = await self.get_prototypenumber_by_tsp_code(tsp_code)

        # Look up terminal's menu binding to get the snapshot A{x}
        menu_snapshot_id = None
        binding = await self.db.scalar(
            select(TerminalMenuBinding).where(
                TerminalMenuBinding.device_id == terminal.device_id
            )
        )
        if binding and binding.loaded_version is not None:
            snap = await self.db.scalar(
                select(MenuVariantSnapshot).where(
                    MenuVariantSnapshot.menu_variant_id == binding.menu_variant_id,
                    MenuVariantSnapshot.version == binding.loaded_version,
                )
            )
            if snap:
                menu_snapshot_id = snap.id

        # Create payment record
        payment = Payment(
            paym_amount=amount,
            paym_ext_id=paym_ext_id,
            paym_tsp_code=tsp_code,
            terminal_id=terminal.id,
            org_id=terminal.org_id,
            paym_state=2,  # Always accepted
            pay_type_id=pay_type_id,
            menu_snapshot_id=menu_snapshot_id,
        )
        self.db.add(payment)
        await self.db.flush()  # Get paym_id

        # Create payment params
        for param_code, param_value in params.items():
            # Get or create param_id
            param_id = await self.get_param_id(prototypenumber, param_code)
            if not param_id:
                param_id = await self.create_param_code(prototypenumber, param_code)

            payment_param = PaymentParam(
                paym_id=payment.paym_id, param_id=param_id, param_value=param_value
            )
            self.db.add(payment_param)

        # Update balance
        await self._update_balance(
            terminal, tsp.tsp_id, amount, menu_snapshot_id=menu_snapshot_id
        )

        await self.db.commit()
        return payment

    async def _update_balance(
        self,
        terminal: Terminal,
        tsp_id: int,
        amount: int,
        menu_snapshot_id: int | None = None,
    ):
        """Update balance_terminal_tsp with upsert pattern."""
        int_day = int(datetime.now(UTC).strftime("%Y%m%d"))

        # Check if record exists
        if menu_snapshot_id is not None:
            cond = and_(
                BalanceTerminalTsp.int_day == int_day,
                BalanceTerminalTsp.terminal_id == terminal.id,
                BalanceTerminalTsp.tsp_id == tsp_id,
                BalanceTerminalTsp.menu_snapshot_id == menu_snapshot_id,
            )
        else:
            cond = and_(
                BalanceTerminalTsp.int_day == int_day,
                BalanceTerminalTsp.terminal_id == terminal.id,
                BalanceTerminalTsp.tsp_id == tsp_id,
                BalanceTerminalTsp.menu_snapshot_id.is_(None),
            )

        result = await self.db.execute(select(BalanceTerminalTsp).where(cond))
        balance = result.scalar_one_or_none()

        if balance:
            # Update existing record
            balance.amount += amount
            balance.count += 1
        else:
            # Create new record
            balance = BalanceTerminalTsp(
                int_day=int_day,
                org_id=terminal.org_id,
                terminal_id=terminal.id,
                tsp_id=tsp_id,
                menu_snapshot_id=menu_snapshot_id,
                amount=amount,
                count=1,
            )
            self.db.add(balance)


def parse_params_string(params_str: str | None) -> dict[int, str]:
    """
    Parse params string from terminal request.

    Format: "1 value1;2 value2;3 value3"
    Returns: {1: "value1", 2: "value2", 3: "value3"}
    """
    params = {}
    if not params_str:
        return params

    for param in params_str.split(";"):
        param = param.strip()
        if not param:
            continue

        # Find separator (space or =)
        sep_idx = param.find("=")
        if sep_idx < 0:
            sep_idx = param.find(" ")

        if sep_idx > 0:
            key = param[:sep_idx].strip()
            value = param[sep_idx + 1 :].strip()
            try:
                params[int(key)] = value
            except ValueError:
                continue

    return params


def verify_signature(signature: str, sign_key: str, cn: str) -> bool:
    """
    Verify payment signature.

    Algorithm: base64_decode(signature) + sign_key -> MD5 -> compare with CN
    """
    if not signature or signature == "EMPTY":
        return True  # No signature required

    try:
        decoded = base64.b64decode(signature).decode("utf-8")
        to_hash = decoded + sign_key
        md5_hash = hashlib.md5(to_hash.encode("utf-8")).hexdigest().upper()
        return md5_hash == cn.upper()
    except ValueError, UnicodeDecodeError:
        return False
