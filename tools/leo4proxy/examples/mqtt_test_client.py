# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "paho-mqtt>=2.0.0",
#     "pydantic>=2.0.0",
#     "tzdata; sys_platform == 'win32'",
# ]
# ///

from __future__ import annotations

import json
import os
import platform
import socket
import ssl
import sys
import threading
import time
import urllib.request
import uuid
from datetime import datetime, timezone
from time import sleep
from typing import Any, Callable

# --- Force UTF-8 for console I/O on Windows (fixes Cyrillic in logs) ---
if sys.platform == "win32":
    os.system("chcp 65001 >nul 2>&1")
    for _stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(_stream, "reconfigure"):
            _stream.reconfigure(encoding="utf-8", errors="replace")
# -----------------------------------------------------------------------

import paho.mqtt.client as mqtt
from paho.mqtt.packettypes import PacketTypes
from pydantic import BaseModel, JsonValue, UUID4
import zoneinfo

try:
    from OpenSSL import crypto
except ImportError:
    crypto = None


class DevTaskLoaded(BaseModel):
    id: UUID4
    method_code: int
    device_id: int
    payload: JsonValue


def current_time_iso_with_offset():
    try:
        moscow_tz = zoneinfo.ZoneInfo("Europe/Moscow")
        now = datetime.now(moscow_tz)
    except Exception:
        # Fallback for Windows systems without tzdata package (MSK = UTC+3)
        from datetime import timezone, timedelta
        moscow_tz = timezone(timedelta(hours=3))
        now = datetime.now(moscow_tz)
    basic = now.strftime("%Y-%m-%dT%H:%M:%S%z")
    if len(basic) >= 5 and basic[-5] in ('+', '-'):
        return basic[:-2] + ":" + basic[-2:]
    return basic


def on_message(client, userdata, msg):
    print("topic = " + msg.topic)
    # UserProperty : [('method_code', '0077')]
    if hasattr(msg.properties, 'CorrelationData'):
        print("Native msg.properties.CorrelationData = " + str(msg.properties.CorrelationData.decode(errors="replace")))
    print(str(msg.payload))
    print(str(msg.properties))
    try:
        payload_data = json.loads(msg.payload.decode("utf-8", errors="replace"))
        if isinstance(payload_data, dict) and 'payload' in payload_data:
            print("payload = " + str(payload_data['payload']))
    except Exception:
        pass

    if msg.topic.endswith("tsk"):
        props = mqtt.Properties(PacketTypes.PUBLISH)
        if hasattr(msg.properties, 'CorrelationData'):
            corr_str = str(msg.properties.CorrelationData.decode(errors="replace"))
            props.UserProperty = [("CorrelationData", corr_str)]
            props.CorrelationData = msg.properties.CorrelationData
            mqttc.publish("dev/" + cert["CN"] + "/req",
                          f"from device req, corr data = {corr_str}",
                          qos=0,
                          properties=props)
            props.clear()
            print(f"req = {msg.topic[:-3]}")
        else:
            return
    elif msg.topic.endswith("rsp"):
        props = mqtt.Properties(PacketTypes.PUBLISH)
        if hasattr(msg.properties, 'CorrelationData'):
            corr_str = str(msg.properties.CorrelationData.decode(errors="replace"))
            props.CorrelationData = msg.properties.CorrelationData
            props.UserProperty = [("status_code", "206"), ("ext_id", "12345")]
            mqttc.publish("dev/" + cert["CN"] + "/res",
                          json.dumps({"description": "from device partial result", "corr_data": f"{corr_str}"}),
                          qos=0,
                          properties=props)
            props.clear()
            props.CorrelationData = msg.properties.CorrelationData
            props.UserProperty = [("status_code", "200"), ("ext_id", "12345")]
            mqttc.publish("dev/" + cert["CN"] + "/res",
                          json.dumps({"description": "from device final result", "corr_data": f"{corr_str}"}),
                          qos=0,
                          properties=props)
            print(str(props))
        else:
            return


# ---------------------------------------------------------------------------
# Remote Diagnostics MVP extension.
#
# Protocol:
# - control goes through existing RPC lifecycle:
#   srv/<SN>/tsk -> dev/<SN>/req -> srv/<SN>/rsp -> dev/<SN>/res
# - stream output goes to:
#   dev/<SN>/out
# - method_code:
#   7000 = CMD_DIAG_STREAM_CONTROL
#   7001 = CMD_DIAG_EXEC
#   7002 = CMD_DIAG_CANCEL
#
# Minimal allowlist for hypothesis/test only:
# - system_info
# - echo
# - time
# - list_commands
#
# No arbitrary shell execution is used here.
# ---------------------------------------------------------------------------

CMD_DIAG_STREAM_CONTROL = 7000
CMD_DIAG_EXEC = 7001
CMD_DIAG_CANCEL = 7002
REMOTE_DIAG_METHODS = {
    CMD_DIAG_STREAM_CONTROL,
    CMD_DIAG_EXEC,
    CMD_DIAG_CANCEL,
}

REMOTE_DIAG_SESSIONS_LOCK = threading.Lock()
REMOTE_DIAG_SESSIONS: dict[str, dict[str, Any]] = {}

REMOTE_DIAG_TEST_MAX_LOG_CHUNKS = int(os.getenv("REMOTE_DIAG_TEST_MAX_LOG_CHUNKS", "10"))


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _json_dumps(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, separators=(",", ":"))


def _decode_json_payload(payload: bytes | str) -> Any:
    if isinstance(payload, bytes):
        payload = payload.decode("utf-8", errors="replace")
    if not payload:
        return {}
    return json.loads(payload)


def _get_user_property(properties: Any, name: str) -> str | None:
    user_properties = getattr(properties, "UserProperty", None) or []
    target = name.lower()

    for item in user_properties:
        if not isinstance(item, (tuple, list)) or len(item) != 2:
            continue
        key, value = item
        if str(key).lower() == target:
            return str(value)

    return None


def _correlation_bytes_to_string(value: bytes | bytearray | str | None) -> str:
    if value is None:
        return ""

    if isinstance(value, str):
        return value

    raw = bytes(value)

    try:
        decoded = raw.decode("utf-8")
        if decoded:
            return decoded
    except UnicodeDecodeError:
        pass

    if len(raw) == 16:
        try:
            return str(uuid.UUID(bytes=raw))
        except ValueError:
            pass

    return raw.hex()


def _get_correlation_data_bytes(msg: mqtt.MQTTMessage) -> bytes | None:
    properties = getattr(msg, "properties", None)

    native_corr = getattr(properties, "CorrelationData", None)
    if native_corr:
        if isinstance(native_corr, bytes):
            return native_corr
        if isinstance(native_corr, bytearray):
            return bytes(native_corr)
        return str(native_corr).encode("utf-8")

    user_corr = (
        _get_user_property(properties, "correlationData")
        or _get_user_property(properties, "CorrelationData")
    )
    if user_corr:
        return user_corr.encode("utf-8")

    return None


def _get_correlation_data_string(msg: mqtt.MQTTMessage) -> str:
    return _correlation_bytes_to_string(_get_correlation_data_bytes(msg))


def _extract_method_code_from_payload(payload_obj: Any) -> int | None:
    if not isinstance(payload_obj, dict):
        return None

    for key in ("method_code", "methodCode", "mc"):
        value = payload_obj.get(key)
        if value is not None:
            try:
                return int(value)
            except (TypeError, ValueError):
                return None

    nested_payload = payload_obj.get("payload")
    if isinstance(nested_payload, dict):
        for key in ("method_code", "methodCode", "mc"):
            value = nested_payload.get(key)
            if value is not None:
                try:
                    return int(value)
                except (TypeError, ValueError):
                    return None

    return None


def _extract_method_code(msg: mqtt.MQTTMessage, payload_obj: Any) -> int | None:
    properties = getattr(msg, "properties", None)
    method_from_property = _get_user_property(properties, "method_code")

    if method_from_property is not None:
        try:
            return int(method_from_property)
        except ValueError:
            return None

    return _extract_method_code_from_payload(payload_obj)


def _extract_rpc_dt(payload_obj: Any) -> list[Any]:
    if isinstance(payload_obj, list):
        return payload_obj

    if not isinstance(payload_obj, dict):
        return []

    candidates: list[Any] = []
    candidates.append(payload_obj)

    payload = payload_obj.get("payload")
    if isinstance(payload, dict):
        candidates.append(payload)

    for candidate in candidates:
        dt = candidate.get("dt")
        if isinstance(dt, list):
            return dt
        if dt is not None:
            return [dt]

    return []


def _next_diag_seq(session_id: str) -> int:
    with REMOTE_DIAG_SESSIONS_LOCK:
        state = REMOTE_DIAG_SESSIONS.setdefault(session_id, {})
        seq = int(state.get("seq", 0))
        state["seq"] = seq + 1
        return seq


def _publish_diag_out(
    client: mqtt.Client,
    *,
    session_id: str,
    kind: str,
    stream: str,
    data: str,
    eof: bool = False,
    exit_code: int | None = None,
    truncated: bool = False,
) -> None:
    envelope: dict[str, Any] = {
        "v": 1,
        "session_id": session_id,
        "seq": _next_diag_seq(session_id),
        "ts": _utc_now_iso(),
        "kind": kind,
        "stream": stream,
        "encoding": "utf-8",
        "data": data,
        "eof": eof,
    }

    if exit_code is not None:
        envelope["exit_code"] = exit_code

    if truncated:
        envelope["truncated"] = True

    topic = "dev/" + cert["CN"] + "/out"
    client.publish(topic, _json_dumps(envelope), qos=0)
    print(f"remote-diagnostics out -> {topic}: {envelope}")


def _publish_diag_result(
    client: mqtt.Client,
    *,
    corr_bytes: bytes,
    method_code: int,
    result: dict[str, Any],
    status_code: str = "200",
) -> None:
    corr_string = _correlation_bytes_to_string(corr_bytes)

    props = mqtt.Properties(PacketTypes.PUBLISH)
    props.CorrelationData = corr_bytes
    props.UserProperty = [
        ("status_code", status_code),
        ("ext_id", "diag-" + str(result.get("session_id", "unknown"))[:8]),
        ("method_code", str(method_code)),
        ("correlationData", corr_string),
    ]

    topic = "dev/" + cert["CN"] + "/res"
    client.publish(topic, _json_dumps(result), qos=0, properties=props)
    print(f"remote-diagnostics res -> {topic}: {result}")


def _truncate_text_by_bytes(text: str, max_bytes: int) -> tuple[str, bool]:
    encoded = text.encode("utf-8")

    if len(encoded) <= max_bytes:
        return text, False

    truncated = encoded[:max_bytes].decode("utf-8", errors="replace")
    return truncated, True


def _diag_command_system_info(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    info = {
        "hostname": socket.gethostname(),
        "platform": platform.platform(),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python": sys.version,
        "cwd": os.getcwd(),
        "device_sn": cert["CN"],
        "ts": _utc_now_iso(),
    }
    return [("stdout", "stdout", json.dumps(info, ensure_ascii=False, indent=2) + "\n")]


def _diag_command_echo(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    text = str(args.get("text") or args.get("message") or "echo from mqtt_test_client")
    return [("stdout", "stdout", text + "\n")]


def _diag_command_time(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    data = {
        "utc": _utc_now_iso(),
        "moscow": current_time_iso_with_offset(),
        "unix": int(time.time()),
    }
    return [("stdout", "stdout", json.dumps(data, ensure_ascii=False, indent=2) + "\n")]


def _diag_command_list_commands(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    data = {
        "commands": sorted(REMOTE_DIAG_COMMANDS.keys()),
        "note": "Minimal safe allowlist for hypothesis/test only. No arbitrary shell execution.",
    }
    return [("stdout", "stdout", json.dumps(data, ensure_ascii=False, indent=2) + "\n")]


# ---------------------------------------------------------------------------
# Subprocess helper
# ---------------------------------------------------------------------------

import subprocess  # noqa: E402


def _run_cmd(cmd: list[str] | str, *, shell: bool = False, timeout: int = 30) -> tuple[str, str, int]:
    """Run a command and return (stdout, stderr, returncode)."""
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            shell=shell,
        )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "", f"command timed out after {timeout}s", -1
    except FileNotFoundError:
        return "", f"command not found: {cmd[0] if isinstance(cmd, list) else cmd}", -1
    except Exception as exc:
        return "", str(exc), -1


def _is_windows() -> bool:
    return platform.system() == "Windows"


def _ps_cmd(script: str) -> list[str]:
    """Build a PowerShell command that outputs UTF-8 (fixes Cyrillic on Russian Windows)."""
    return ["powershell", "-NoProfile", "-Command", f"chcp 65001 >$null; {script}"]


# ---------------------------------------------------------------------------
# Universal commands
# ---------------------------------------------------------------------------

def _diag_command_network_info(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    if _is_windows():
        stdout, stderr, rc = _run_cmd(["ipconfig", "/all"], timeout=15)
        if rc != 0 and stderr:
            return [("stderr", "stderr", stderr)]
        return [("stdout", "stdout", stdout)]
    else:
        outputs: list[tuple[str, str, str]] = []
        for cmd_label, cmd in [
            ("ip addr", ["ip", "addr"]),
            ("ip route", ["ip", "route"]),
        ]:
            stdout, stderr, rc = _run_cmd(cmd, timeout=10)
            if stdout:
                outputs.append(("stdout", "stdout", f"--- {cmd_label} ---\n{stdout}"))
            if stderr:
                outputs.append(("stderr", "stderr", f"--- {cmd_label} (stderr) ---\n{stderr}"))
        if not outputs:
            outputs.append(("stderr", "stderr", "no network info available\n"))
        return outputs


def _diag_command_disk_usage(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    if _is_windows():
        stdout, stderr, rc = _run_cmd(
            _ps_cmd("Get-PSDrive -PSProvider FileSystem | Select-Object Name, @{N='Used(GB)';E={[math]::Round($_.Used/1GB,2)}}, @{N='Free(GB)';E={[math]::Round($_.Free/1GB,2)}}, Root | Format-Table -AutoSize"),
            timeout=15,
        )
        if rc != 0 and stderr:
            return [("stderr", "stderr", stderr)]
        return [("stdout", "stdout", stdout)]
    else:
        stdout, stderr, rc = _run_cmd(["df", "-h"], timeout=10)
        if rc != 0 and stderr:
            return [("stderr", "stderr", stderr)]
        return [("stdout", "stdout", stdout)]


def _diag_command_service_status(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    if _is_windows():
        stdout, stderr, rc = _run_cmd(
            _ps_cmd("Get-Service | Select-Object Name, Status, DisplayName | Format-Table -AutoSize"),
            timeout=20,
        )
        if rc != 0 and stderr:
            return [("stderr", "stderr", stderr)]
        return [("stdout", "stdout", stdout)]
    else:
        stdout, stderr, rc = _run_cmd(["systemctl", "list-units", "--type=service", "--no-pager"], timeout=15)
        if rc != 0 and stderr:
            return [("stderr", "stderr", stderr)]
        return [("stdout", "stdout", stdout)]


# ---------------------------------------------------------------------------
# Linux commands
# ---------------------------------------------------------------------------

def _diag_command_uptime(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(["uptime"], timeout=10)
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_memory_usage(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(["free", "-h"], timeout=10)
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_process_list(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    if _is_windows():
        stdout, stderr, rc = _run_cmd(
            _ps_cmd("Get-Process | Select-Object Id, ProcessName, CPU, WorkingSet | Sort-Object CPU -Descending | Select-Object -First 50 | Format-Table -AutoSize"),
            timeout=20,
        )
    else:
        stdout, stderr, rc = _run_cmd(["ps", "aux"], timeout=15)
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_top_processes(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    if _is_windows():
        stdout, stderr, rc = _run_cmd(
            _ps_cmd("Get-Process | Sort-Object CPU -Descending | Select-Object -First 20 Id, ProcessName, CPU, @{N='Mem(MB)';E={[math]::Round($_.WorkingSet/1MB,1)}} | Format-Table -AutoSize"),
            timeout=20,
        )
    else:
        stdout, stderr, rc = _run_cmd(
            ["bash", "-c", "ps aux --sort=-%cpu | head -21"],
            timeout=15,
        )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_journal_logs(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    lines = int(args.get("lines") or 50)
    unit = str(args.get("unit") or "").strip()

    cmd = ["journalctl", "--no-pager", "-n", str(lines)]
    if unit:
        cmd.extend(["-u", unit])

    stdout, stderr, rc = _run_cmd(cmd, timeout=20)
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_iptables_rules(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    outputs: list[tuple[str, str, str]] = []
    for cmd_label, cmd in [
        ("iptables -L -n", ["iptables", "-L", "-n", "-v"]),
        ("nft list ruleset", ["nft", "list", "ruleset"]),
    ]:
        stdout, stderr, rc = _run_cmd(cmd, timeout=10)
        if rc == 0 and stdout:
            outputs.append(("stdout", "stdout", f"--- {cmd_label} ---\n{stdout}"))
    if not outputs:
        outputs.append(("stderr", "stderr", "no firewall rules found or insufficient permissions\n"))
    return outputs


def _diag_command_systemctl_status(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    unit = str(args.get("unit") or "").strip()
    if unit:
        cmd = ["systemctl", "status", unit, "--no-pager"]
    else:
        cmd = ["systemctl", "list-units", "--type=service", "--state=running", "--no-pager"]

    stdout, stderr, rc = _run_cmd(cmd, timeout=15)
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout or "(no output)")]


# ---------------------------------------------------------------------------
# Windows commands
# ---------------------------------------------------------------------------

def _diag_command_get_processes(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(
        _ps_cmd("Get-Process | Select-Object Id, ProcessName, CPU, @{N='Mem(MB)';E={[math]::Round($_.WorkingSet/1MB,1)}} | Sort-Object CPU -Descending | Format-Table -AutoSize"),
        timeout=20,
    )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_get_services(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(
        _ps_cmd("Get-Service | Select-Object Name, Status, DisplayName | Format-Table -AutoSize"),
        timeout=20,
    )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_event_log(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    log_name = str(args.get("logName") or "System")
    count = int(args.get("count") or 20)
    count = min(count, 100)

    stdout, stderr, rc = _run_cmd(
        _ps_cmd(
            f"Get-WinEvent -LogName '{log_name}' -MaxEvents {count} | "
            "Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, Message | "
            "Format-Table -AutoSize -Wrap"),
        timeout=30,
    )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout or "(no events)")]


def _diag_command_disk_info(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(
        _ps_cmd(
            "Get-CimInstance Win32_LogicalDisk | "
            "Select-Object DeviceID, FileSystem, @{N='SizeGB';E={[math]::Round($_.Size/1GB,2)}}, "
            "@{N='FreeGB';E={[math]::Round($_.FreeSpace/1GB,2)}}, VolumeName | Format-Table -AutoSize"),
        timeout=20,
    )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_cpu_usage(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(
        _ps_cmd(
            "$cpu = Get-CimInstance Win32_Processor; "
            "$load = (Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor | Where-Object {$_.Name -eq '_Total'}).PercentProcessorTime; "
            "$cpu | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed | Format-List; "
            "Write-Output \"Current CPU Load: $load%\""),
        timeout=15,
    )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_network_config(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(["ipconfig", "/all"], timeout=15)
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_os_version(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    stdout, stderr, rc = _run_cmd(
        _ps_cmd(
            "Get-CimInstance Win32_OperatingSystem | "
            "Select-Object Caption, Version, BuildNumber, OSArchitecture, InstallDate, LastBootUpTime | Format-List"),
        timeout=15,
    )
    if rc != 0 and stderr:
        return [("stderr", "stderr", stderr)]
    return [("stdout", "stdout", stdout)]


def _diag_command_mssql_query(args: dict[str, Any]) -> list[tuple[str, str, str]]:
    table = str(args.get("table") or "").strip()
    if not table:
        return [("stderr", "stderr", "error: 'table' argument is required\n")]

    # Basic validation: allow only alphanumeric, underscore, dot, bracket
    import re
    if not re.match(r'^[\w.\[\]]+$', table):
        return [("stderr", "stderr", f"error: invalid table name: {table!r}\n")]

    # Environment variables for MS SQL connection (Windows Authentication)
    mssql_server = os.getenv("MSSQL_SERVER", r"localhost\SQLEXPRESS")
    mssql_database = os.getenv("MSSQL_DATABASE", "Terminal")

    # Try pyodbc with Windows Authentication (Trusted Connection)
    try:
        import pyodbc  # type: ignore

        # Try known drivers in order of preference
        drivers = [
            "{ODBC Driver 17 for SQL Server}",
            "{ODBC Driver 18 for SQL Server}",
            "{SQL Server Native Client 11.0}",
            "{SQL Server}",
        ]

        conn = None
        last_err = None
        for driver in drivers:
            conn_str = (
                f"DRIVER={driver};"
                f"SERVER={mssql_server};"
                f"DATABASE={mssql_database};"
                f"Trusted_Connection=yes;"
            )
            try:
                conn = pyodbc.connect(conn_str, timeout=10)
                break
            except Exception as e:
                last_err = e
                continue

        if conn is None:
            return [("stderr", "stderr", f"pyodbc: no suitable ODBC driver found. Last error: {last_err}\n")]
        cursor = conn.cursor()
        cursor.execute(f"SELECT TOP 100 * FROM {table}")
        columns = [desc[0] for desc in cursor.description]
        rows = cursor.fetchall()
        conn.close()

        lines = ["\t".join(columns)]
        for row in rows:
            lines.append("\t".join(str(v) for v in row))
        return [("stdout", "stdout", "\n".join(lines) + "\n")]
    except ImportError:
        pass
    except Exception as exc:
        return [("stderr", "stderr", f"pyodbc error: {exc}\n")]

    # Fallback: PowerShell Invoke-Sqlcmd with Windows Authentication
    ps_script = (
        f"Invoke-Sqlcmd -Query 'SELECT TOP 100 * FROM {table}' "
        f"-ServerInstance '{mssql_server}' -Database '{mssql_database}' "
        "| Format-Table -AutoSize"
    )
    stdout, stderr, rc = _run_cmd(
        _ps_cmd(ps_script),
        timeout=30,
    )
    if rc != 0:
        return [("stderr", "stderr", stderr or f"mssql_query failed (exit {rc}). Set MSSQL_SERVER/MSSQL_DATABASE env vars.\n")]
    return [("stdout", "stdout", stdout or "(no rows)")]


# ---------------------------------------------------------------------------
# Command registry
# ---------------------------------------------------------------------------

REMOTE_DIAG_COMMANDS: dict[str, Callable[[dict[str, Any]], list[tuple[str, str, str]]]] = {
    # existing test commands
    "system_info": _diag_command_system_info,
    "echo": _diag_command_echo,
    "time": _diag_command_time,
    "list_commands": _diag_command_list_commands,
    # universal
    "network_info": _diag_command_network_info,
    "disk_usage": _diag_command_disk_usage,
    "service_status": _diag_command_service_status,
    # linux
    "uptime": _diag_command_uptime,
    "memory_usage": _diag_command_memory_usage,
    "process_list": _diag_command_process_list,
    "top_processes": _diag_command_top_processes,
    "journal_logs": _diag_command_journal_logs,
    "iptables_rules": _diag_command_iptables_rules,
    "systemctl_status": _diag_command_systemctl_status,
    # windows
    "get_processes": _diag_command_get_processes,
    "get_services": _diag_command_get_services,
    "event_log": _diag_command_event_log,
    "disk_info": _diag_command_disk_info,
    "cpu_usage": _diag_command_cpu_usage,
    "network_config": _diag_command_network_config,
    "os_version": _diag_command_os_version,
    "mssql_query": _diag_command_mssql_query,
}


def _live_log_worker(
    client: mqtt.Client,
    *,
    session_id: str,
    stream: str,
    level: str,
    ttl_sec: int,
    max_rate_bps: int,
) -> None:
    started_at = time.monotonic()
    chunk_no = 0

    try:
        _publish_diag_out(
            client,
            session_id=session_id,
            kind="status",
            stream=stream,
            data=(
                f"live log started: level={level}, ttl_sec={ttl_sec}, "
                f"max_rate_bps={max_rate_bps}, test_max_chunks={REMOTE_DIAG_TEST_MAX_LOG_CHUNKS}\n"
            ),
        )

        while True:
            with REMOTE_DIAG_SESSIONS_LOCK:
                state = REMOTE_DIAG_SESSIONS.get(session_id) or {}
                stop_event = state.get("stop_event")

            if isinstance(stop_event, threading.Event) and stop_event.is_set():
                break

            if time.monotonic() - started_at >= ttl_sec:
                break

            if chunk_no >= REMOTE_DIAG_TEST_MAX_LOG_CHUNKS:
                break

            chunk_no += 1
            _publish_diag_out(
                client,
                session_id=session_id,
                kind="log",
                stream=stream,
                data=(
                    f"[{_utc_now_iso()}] test live log chunk={chunk_no} "
                    f"sn={cert['CN']} level={level}\n"
                ),
            )

            sleep(1)

        _publish_diag_out(
            client,
            session_id=session_id,
            kind="status",
            stream=stream,
            data="live log finished\n",
            eof=True,
            exit_code=0,
        )
    except Exception as exc:
        _publish_diag_out(
            client,
            session_id=session_id,
            kind="error",
            stream=stream,
            data=f"live log worker error: {exc}\n",
            eof=True,
            exit_code=1,
        )
    finally:
        with REMOTE_DIAG_SESSIONS_LOCK:
            REMOTE_DIAG_SESSIONS.pop(session_id, None)


def _handle_diag_stream_control(
    client: mqtt.Client,
    *,
    corr_bytes: bytes,
    payload_item: dict[str, Any],
) -> None:
    session_id = str(payload_item.get("session_id") or "")
    action = str(payload_item.get("action") or "").lower()
    stream = str(payload_item.get("stream") or "esp32-log")
    level = str(payload_item.get("level") or "info")
    ttl_sec = int(payload_item.get("ttl_sec") or 300)
    max_rate_bps = int(payload_item.get("max_rate_bps") or 8192)

    if not session_id:
        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_STREAM_CONTROL,
            status_code="400",
            result={
                "status": "ERROR",
                "method_code": CMD_DIAG_STREAM_CONTROL,
                "error": "missing session_id",
                "output_topic": "dev/" + cert["CN"] + "/out",
            },
        )
        return

    if action == "start":
        stop_event = threading.Event()

        with REMOTE_DIAG_SESSIONS_LOCK:
            old_state = REMOTE_DIAG_SESSIONS.get(session_id)
            old_stop_event = old_state.get("stop_event") if isinstance(old_state, dict) else None
            if isinstance(old_stop_event, threading.Event):
                old_stop_event.set()

            REMOTE_DIAG_SESSIONS[session_id] = {
                "seq": 0,
                "stop_event": stop_event,
                "stream": stream,
                "kind": "live_log",
            }

        thread = threading.Thread(
            target=_live_log_worker,
            kwargs={
                "client": client,
                "session_id": session_id,
                "stream": stream,
                "level": level,
                "ttl_sec": ttl_sec,
                "max_rate_bps": max_rate_bps,
            },
            daemon=True,
        )
        thread.start()

        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_STREAM_CONTROL,
            result={
                "status": "OK",
                "method_code": CMD_DIAG_STREAM_CONTROL,
                "session_id": session_id,
                "action": "start",
                "stream": stream,
                "output_topic": "dev/" + cert["CN"] + "/out",
                "truncated": False,
            },
        )
        return

    if action == "stop":
        stopped = False

        with REMOTE_DIAG_SESSIONS_LOCK:
            state = REMOTE_DIAG_SESSIONS.get(session_id)
            stop_event = state.get("stop_event") if isinstance(state, dict) else None
            if isinstance(stop_event, threading.Event):
                stop_event.set()
                stopped = True

        _publish_diag_out(
            client,
            session_id=session_id,
            kind="status",
            stream=stream,
            data=("stop requested\n" if stopped else "stop requested, session was not active\n"),
            eof=not stopped,
            exit_code=0,
        )

        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_STREAM_CONTROL,
            result={
                "status": "OK",
                "method_code": CMD_DIAG_STREAM_CONTROL,
                "session_id": session_id,
                "action": "stop",
                "stream": stream,
                "output_topic": "dev/" + cert["CN"] + "/out",
                "was_active": stopped,
                "truncated": False,
            },
        )
        return

    _publish_diag_result(
        client,
        corr_bytes=corr_bytes,
        method_code=CMD_DIAG_STREAM_CONTROL,
        status_code="400",
        result={
            "status": "ERROR",
            "method_code": CMD_DIAG_STREAM_CONTROL,
            "session_id": session_id,
            "error": "unsupported action, expected start or stop",
            "output_topic": "dev/" + cert["CN"] + "/out",
        },
    )


def _exec_worker(
    client: mqtt.Client,
    *,
    session_id: str,
    command_id: str,
    args: dict[str, Any],
    max_output_bytes: int,
    corr_bytes: bytes,
    stop_event: threading.Event,
) -> None:
    _publish_diag_out(
        client,
        session_id=session_id,
        kind="status",
        stream="agent",
        data=f"exec started: command_id={command_id}\n",
    )

    exit_code = 0
    truncated = False

    try:
        if stop_event.is_set():
            _publish_diag_out(
                client,
                session_id=session_id,
                kind="status",
                stream="agent",
                data="exec cancelled before execution\n",
                eof=True,
                exit_code=1,
            )
            _publish_diag_result(
                client,
                corr_bytes=corr_bytes,
                method_code=CMD_DIAG_EXEC,
                result={
                    "status": "OK",
                    "method_code": CMD_DIAG_EXEC,
                    "session_id": session_id,
                    "command_id": command_id,
                    "exit_code": 1,
                    "output_topic": "dev/" + cert["CN"] + "/out",
                    "cancelled": True,
                },
            )
            return

        records = REMOTE_DIAG_COMMANDS[command_id](args)
        remaining_bytes = max_output_bytes

        for kind, stream, text in records:
            if stop_event.is_set():
                truncated = True
                break
            if remaining_bytes <= 0:
                truncated = True
                break

            chunk_size = 4096
            encoded = text.encode("utf-8")
            for offset in range(0, max(len(encoded), 1), chunk_size):
                if stop_event.is_set():
                    truncated = True
                    break
                part = encoded[offset : offset + chunk_size].decode("utf-8", errors="replace")
                part_chunk, chunk_truncated = _truncate_text_by_bytes(part, remaining_bytes)
                remaining_bytes -= len(part_chunk.encode("utf-8"))
                truncated = truncated or chunk_truncated

                _publish_diag_out(
                    client,
                    session_id=session_id,
                    kind=kind,
                    stream=stream,
                    data=part_chunk,
                    truncated=chunk_truncated,
                )
                if chunk_truncated or remaining_bytes <= 0:
                    break

        _publish_diag_out(
            client,
            session_id=session_id,
            kind="result",
            stream="agent",
            data="exec finished\n",
            eof=True,
            exit_code=exit_code,
            truncated=truncated,
        )

        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_EXEC,
            result={
                "status": "OK",
                "method_code": CMD_DIAG_EXEC,
                "session_id": session_id,
                "command_id": command_id,
                "exit_code": exit_code,
                "output_topic": "dev/" + cert["CN"] + "/out",
                "truncated": truncated,
            },
        )
    except Exception as exc:
        exit_code = 1
        _publish_diag_out(
            client,
            session_id=session_id,
            kind="error",
            stream="stderr",
            data=f"exec error: {exc}\n",
            eof=True,
            exit_code=exit_code,
        )
        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_EXEC,
            status_code="500",
            result={
                "status": "ERROR",
                "method_code": CMD_DIAG_EXEC,
                "session_id": session_id,
                "command_id": command_id,
                "exit_code": exit_code,
                "output_topic": "dev/" + cert["CN"] + "/out",
                "error": str(exc),
                "truncated": truncated,
            },
        )
    finally:
        with REMOTE_DIAG_SESSIONS_LOCK:
            REMOTE_DIAG_SESSIONS.pop(session_id, None)


def _handle_diag_exec(
    client: mqtt.Client,
    *,
    corr_bytes: bytes,
    payload_item: dict[str, Any],
) -> None:
    session_id = str(payload_item.get("session_id") or "")
    command_id = str(payload_item.get("command_id") or "")
    args = payload_item.get("args") if isinstance(payload_item.get("args"), dict) else {}
    max_output_bytes = int(payload_item.get("max_output_bytes") or 1048576)

    if not session_id:
        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_EXEC,
            status_code="400",
            result={
                "status": "ERROR",
                "method_code": CMD_DIAG_EXEC,
                "command_id": command_id,
                "error": "missing session_id",
                "output_topic": "dev/" + cert["CN"] + "/out",
                "truncated": False,
            },
        )
        return

    if command_id not in REMOTE_DIAG_COMMANDS:
        _publish_diag_out(
            client,
            session_id=session_id,
            kind="error",
            stream="stderr",
            data=(
                f"command_id is not allowed: {command_id!r}. "
                f"Allowed: {', '.join(sorted(REMOTE_DIAG_COMMANDS.keys()))}\n"
            ),
            eof=True,
            exit_code=1,
        )
        _publish_diag_result(
            client,
            corr_bytes=corr_bytes,
            method_code=CMD_DIAG_EXEC,
            status_code="400",
            result={
                "status": "ERROR",
                "method_code": CMD_DIAG_EXEC,
                "session_id": session_id,
                "command_id": command_id,
                "exit_code": 1,
                "output_topic": "dev/" + cert["CN"] + "/out",
                "error": "command_id is not in local allowlist",
                "truncated": False,
            },
        )
        return

    stop_event = threading.Event()
    with REMOTE_DIAG_SESSIONS_LOCK:
        old_state = REMOTE_DIAG_SESSIONS.get(session_id)
        old_stop_event = old_state.get("stop_event") if isinstance(old_state, dict) else None
        if isinstance(old_stop_event, threading.Event):
            old_stop_event.set()

        REMOTE_DIAG_SESSIONS[session_id] = {
            "seq": 0,
            "stop_event": stop_event,
            "command_id": command_id,
            "kind": "exec",
        }

    thread = threading.Thread(
        target=_exec_worker,
        kwargs={
            "client": client,
            "session_id": session_id,
            "command_id": command_id,
            "args": args,
            "max_output_bytes": max_output_bytes,
            "corr_bytes": corr_bytes,
            "stop_event": stop_event,
        },
        daemon=True,
    )
    thread.start()


def _handle_diag_cancel(
    client: mqtt.Client,
    *,
    corr_bytes: bytes,
    payload_item: dict[str, Any],
) -> None:
    session_id = str(payload_item.get("session_id") or "")
    reason = payload_item.get("reason")

    cancelled = False

    if session_id:
        with REMOTE_DIAG_SESSIONS_LOCK:
            state = REMOTE_DIAG_SESSIONS.get(session_id)
            stop_event = state.get("stop_event") if isinstance(state, dict) else None
            if isinstance(stop_event, threading.Event):
                stop_event.set()
                cancelled = True

        _publish_diag_out(
            client,
            session_id=session_id,
            kind="status",
            stream="agent",
            data=f"cancel requested: reason={reason or 'not specified'}\n",
            eof=not cancelled,
            exit_code=0,
        )

    _publish_diag_result(
        client,
        corr_bytes=corr_bytes,
        method_code=CMD_DIAG_CANCEL,
        result={
            "status": "OK",
            "method_code": CMD_DIAG_CANCEL,
            "session_id": session_id,
            "reason": reason,
            "cancelled": cancelled,
            "output_topic": "dev/" + cert["CN"] + "/out",
            "truncated": False,
        },
    )


def handle_remote_diagnostics_message(client, userdata, msg) -> bool:
    if not msg.topic.endswith("rsp"):
        return False

    try:
        payload_obj = _decode_json_payload(msg.payload)
    except Exception as exc:
        print(f"remote-diagnostics: cannot parse rsp payload as JSON: {exc}")
        return False

    method_code = _extract_method_code(msg, payload_obj)

    if method_code not in REMOTE_DIAG_METHODS:
        return False

    corr_bytes = _get_correlation_data_bytes(msg)
    if not corr_bytes:
        print("remote-diagnostics: drop RPC rsp because correlationData is missing")
        return True

    dt = _extract_rpc_dt(payload_obj)
    payload_item = dt[0] if dt and isinstance(dt[0], dict) else {}

    print(
        "remote-diagnostics: handle "
        f"method_code={method_code}, corr={_correlation_bytes_to_string(corr_bytes)}, "
        f"payload_item={payload_item}"
    )

    if method_code == CMD_DIAG_STREAM_CONTROL:
        _handle_diag_stream_control(client, corr_bytes=corr_bytes, payload_item=payload_item)
        return True

    if method_code == CMD_DIAG_EXEC:
        _handle_diag_exec(client, corr_bytes=corr_bytes, payload_item=payload_item)
        return True

    if method_code == CMD_DIAG_CANCEL:
        _handle_diag_cancel(client, corr_bytes=corr_bytes, payload_item=payload_item)
        return True

    return False


def on_message_with_remote_diagnostics(client, userdata, msg):
    if handle_remote_diagnostics_message(client, userdata, msg):
        return

    on_message(client, userdata, msg)


g_is_connected: bool = False


# mqtt callbacks
def on_connect(client, userdata, flags, reason_code, properties=None):
    global g_is_connected
    g_is_connected = True
    print(f"MQTT on_connect: reason_code={reason_code}")
    print(f"userdata={userdata}")
    print(f"flags={flags}")
    print(f"properties={properties}")
    client.subscribe("srv/" + cert["CN"] + "/tsk", qos=0)
    client.subscribe("srv/" + cert["CN"] + "/rsp", qos=1)
    client.subscribe("srv/" + cert["CN"] + "/eva", qos=0)
    client.subscribe("srv/" + cert["CN"] + "/cmt", qos=0)


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties=None):
    global g_is_connected
    g_is_connected = False
    print(f"MQTT on_disconnect: reason_code={reason_code}, flags={disconnect_flags}")


def on_subscribe(client, userdata, mid, reason_code_list, properties=None):
    print(f"MQTT Subscribed (mid={mid}, rc={reason_code_list})")


def on_publish(client, userdata, mid, reason_code=None, properties=None):
    pass


# ---------------------------------------------------------------------------
# Proxy / Certificate resolution
# ---------------------------------------------------------------------------

PROXY_HTTP_URL = os.getenv("LEO4_PROXY_HTTP", "http://127.0.0.1:18443")
MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "main_app")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")
MQTT_POLL_INTERVAL_SEC = int(os.getenv("MQTT_POLL_INTERVAL_SEC", "60"))
MQTT_SEND_TEST_EVENTS = os.getenv("MQTT_SEND_TEST_EVENTS", "0") == "1"
MQTT_MAX_ITERATIONS = int(os.getenv("MQTT_MAX_ITERATIONS", "0"))

# Legacy file cert variables (optional fallback)
MQTT_CA_CERT = os.getenv("MQTT_CA_CERT", "")
MQTT_CLIENT_CERT = os.getenv("MQTT_CLIENT_CERT", "")
MQTT_CLIENT_KEY = os.getenv("MQTT_CLIENT_KEY", "")

cert: dict[str, str] = {}

def resolve_certificate() -> dict[str, str]:
    resolved_cert: dict[str, str] = {}

    # 1. Attempt to obtain certificate details from Leo4Proxy info API
    if PROXY_HTTP_URL:
        try:
            info_req = urllib.request.Request(f"{PROXY_HTTP_URL}/_leo4/info", headers={"User-Agent": "Leo4MqttClient/1.0"})
            with urllib.request.urlopen(info_req, timeout=3) as resp:
                info_data = json.loads(resp.read().decode("utf-8"))
                sn = info_data.get("sn", "")
                resolved_cert["CN"] = sn
                resolved_cert["sn"] = sn
                resolved_cert["client_id"] = info_data.get("client_id", sn)
                resolved_cert["urn"] = info_data.get("urn", "")
                resolved_cert["email"] = info_data.get("email", "")
                resolved_cert["serial"] = info_data.get("serial", "")
                resolved_cert["thumbprint"] = info_data.get("thumbprint", "")
                subject = info_data.get("subject", "")
                for part in subject.split(","):
                    if "=" in part:
                        k, v = part.strip().split("=", 1)
                        resolved_cert[k.strip()] = v.strip()
                print(f"[LEO4-PROXY] Loaded certificate from Leo4Proxy info API: CN={resolved_cert.get('CN')}, client_id={resolved_cert.get('client_id')}, email={resolved_cert.get('email')}")
        except Exception as e:
            print(f"[LEO4-PROXY] Note: Could not query local proxy info ({e})")

    # 2. Attempt fallback to PEM file if specified and crypto is installed
    if not resolved_cert.get("CN") and MQTT_CLIENT_CERT and os.path.isfile(MQTT_CLIENT_CERT) and crypto is not None:
        try:
            with open(MQTT_CLIENT_CERT, 'rb') as pem_file:
                x509 = crypto.load_certificate(crypto.FILETYPE_PEM, pem_file.read())
                resolved_cert = {name.decode(): value.decode('utf-8') for name, value in x509.get_subject().get_components()}
                print(f"[FALLBACK] Loaded certificate from file {MQTT_CLIENT_CERT}: CN={resolved_cert.get('CN')}")
        except Exception as e:
            print(f"[FALLBACK] Failed to read certificate file: {e}")

    # 3. Fallback to DEVICE_SN environment variable or default
    if not resolved_cert.get("CN"):
        default_sn = os.getenv("DEVICE_SN", "a4b0000773c82116d210826")
        resolved_cert["CN"] = default_sn
        print(f"[FALLBACK] Using configured Device SN: CN={resolved_cert['CN']}")

    return resolved_cert


def main():
    global cert, mqttc

    cert.update(resolve_certificate())
    print(f"Active Device CN/SN: {cert['CN']}")

    mqtt_client_id = os.getenv("MQTT_CLIENT_ID") or cert.get("CN") or cert.get("sn")

    mqttc = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        protocol=mqtt.MQTTv5,
        client_id=mqtt_client_id,
    )

    mqttc.on_message = on_message_with_remote_diagnostics
    mqttc.on_connect = on_connect
    mqttc.on_disconnect = on_disconnect
    mqttc.on_subscribe = on_subscribe
    mqttc.on_publish = on_publish

    # Fast reconnect backoff (retry every 1-3 seconds instead of default 120s)
    mqttc.reconnect_delay_set(min_delay=1, max_delay=3)

    if MQTT_USERNAME:
        mqttc.username_pw_set(username=MQTT_USERNAME, password=MQTT_PASSWORD or None)

    # If direct mTLS without proxy is explicitly requested with cert files
    if os.getenv("MQTT_DIRECT_TLS", "0") == "1" and MQTT_CA_CERT and MQTT_CLIENT_CERT and MQTT_CLIENT_KEY:
        print(f"[DIRECT-TLS] Enabling direct OpenSSL TLS to {MQTT_HOST}:{MQTT_PORT}...")
        mqttc.tls_set(
            MQTT_CA_CERT,
            MQTT_CLIENT_CERT,
            MQTT_CLIENT_KEY,
            tls_version=ssl.PROTOCOL_TLSv1_2,
        )
    else:
        print(f"[MAIN-APP] Connecting to Mosquitto Bridge at {MQTT_HOST}:{MQTT_PORT} (plain TCP, user: '{MQTT_USERNAME or 'anonymous'}')...")

    mqttc.connect(host=MQTT_HOST, port=MQTT_PORT, keepalive=60)
    mqttc.loop_start()

    i = 0
    try:
        while True:
            if not g_is_connected:
                print(f"[CLIENT] Waiting for broker connection (127.0.0.1:{MQTT_PORT})...")
                sleep(1)
                continue

            i = i + 1

            # Polling RPC request
            zero_corr = str(uuid.UUID(int=0))
            props = mqtt.Properties(PacketTypes.PUBLISH)
            props.CorrelationData = zero_corr.encode("utf-8")
            props.UserProperty = [("correlationData", zero_corr)]
            mqttc.publish(
                "dev/" + cert["CN"] + "/req",
                f"poll req = {i}",
                qos=0,
                properties=props,
            )
            print(f"device side, poll req sent, iteration = {i}")

            if MQTT_SEND_TEST_EVENTS:
                props.clear()
                event_id = 36823 + i
                corr_id = str(uuid.uuid4())
                ts_now = int(time.time())
                props.UserProperty = [
                    ("event_type_code", "888"),
                    ("dev_event_id", str(event_id)),
                    ("dev_timestamp", str(ts_now)),
                    ("correlation_id", corr_id),
                ]
                event_payload = {
                    "101": event_id,
                    "102": current_time_iso_with_offset(),
                    "200": 888,
                    "300": [
                        {
                            "301": "044AFE42C76781",
                            "302": 6,
                            "303": 0,
                        }
                    ],
                }
                mqttc.publish(
                    "dev/" + cert["CN"] + "/evt",
                    json.dumps(event_payload),
                    qos=1,
                    retain=False,
                    properties=props,
                )
                print(f"device side, event sent (type 888, id {event_id}), iteration = {i}")

                props.clear()
                did = 10000 + i
                props.UserProperty = [("event_type_code", "44"), ("dev_event_id", f"{did}")]
                gauge = {
                    "101": did,
                    "102": current_time_iso_with_offset(),
                    "200": 44,
                    "300": [
                        {
                            "310": "test-python-paho-client",
                            "323": "192.168.1.101",
                            "324": cert["CN"],
                        }
                    ],
                }
                mqttc.publish(
                    "dev/" + cert["CN"] + "/req",
                    json.dumps(gauge),
                    qos=0,
                    properties=props,
                )
                print(f"device side, prepare send gauge = {gauge}, test iteration = {i}")

            if MQTT_MAX_ITERATIONS > 0 and i >= MQTT_MAX_ITERATIONS:
                print(f"device side, reached max iterations ({MQTT_MAX_ITERATIONS}), exiting.")
                break

            sleep(MQTT_POLL_INTERVAL_SEC)
    except KeyboardInterrupt:
        print("\nStopping MQTT client (KeyboardInterrupt)...")
    finally:
        print("Stopping MQTT client loop and disconnecting...")
        mqttc.loop_stop()
        mqttc.disconnect()


if __name__ == "__main__":
    main()
