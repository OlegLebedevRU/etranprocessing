import pytest

from l4mcp.server import mcp


@pytest.mark.asyncio
async def test_read_only_tools_are_distinct_from_mutations() -> None:
    tools = {tool.name: tool for tool in await mcp.list_tools()}
    for name in (
        "certificate_summary",
        "inspect_terminals",
        "console_preflight",
        "report_payments_tool",
        "report_balance_by_terminal_tool",
        "report_balance_by_tsp_tool",
        "report_inkass_tool",
    ):
        annotations = tools[name].annotations
        assert annotations is not None
        assert annotations.read_only_hint is True

    for name in ("issue_certificate_pins", "revoke_pending_pin", "console_run"):
        annotations = tools[name].annotations
        assert annotations is None or annotations.read_only_hint is not True
