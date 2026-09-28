# Handoff Report: Приёмка опубликованного Агента (L4D-17A-TOOLS)

## Candidate H-L4D-17A-TOOLS-v1

```yaml
handoff_id: H-L4D-17A-TOOLS-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - FIXTURES
producer_prompt_id: L4D-17A-TOOLS
producer_scope_project: tools
producer_report_path: tools/docs/l4desk/handoffs/L4D-17A-TOOLS-report.md
producer_branch: l4desk/l4d-17a-tools
producer_commit: c4be01b0433fb90782f5332d2b9e82cee64d0dd2
accepted_at_utc: '2026-09-25T18:15:00Z'
contract_version: 1.0.0
schema_revision: 2026-09-17-v1
artifact_version: 1.7.7
artifact_paths:
  - tools/docs/l4desk/contracts/agent_compatibility_contract_v1.json
  - tools/docs/l4desk/fixtures/golden_vectors_v1.json
  - tools/docs/l4desk/fixtures/baseline_capabilities.json
  - tools/tests/test_agent_compatibility_contract_v1.py
artifact_sha256:
  - 38e4ae5f13d563b3ae57a83259d63f9a63049d528d9667ae33efbd5a1e71267a
  - b4f3c1a465e88babf89c0850cfd8dffc29a4cd921ec51a73e2112e6cf9c3034b
  - 375412eaa61e31e72b2ea5f002862a1d1f02becdd4626a71af31d9400df560cb
  - 7e6ce52fc9351cc8a95134bcde0304fc208e0fdbec3287f279b2a7bfd0db0d52
artifact_identity:
  component: l4tools
  version: 1.7.7
  git_sha: 297068f8044b5e40e51257e828c50d6137c9af7c
  sha256: 874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88
  size_bytes: 28730368
  registry_url: https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe
  deployment_status: PUBLISHED
  deployed_environment: artifact-registry
  substitution_used: false
compatibility:
  backward_compatible_with:
    - 1.7.6
    - 1.7.7
  min_supported_version: 1.7.6
  breaking_changes: false
  notes: >
    Agent Compatibility Contract v1 (1.0.0) и golden vectors покрывают
    опубликованные релизы 1.7.6 и 1.7.7. Digests контракта/схем/fixtures
    байтово совпадают с H-L4D-01A-TOOLS-v1. Локальная сборка не подменяла
    опубликованный artifact. MQTT client не создавался и не изменялся.
checks:
  sequence_gate: PASS
  artifact_identity: PASS
  golden_fixtures: PASS
  regression_certificate_bootstrap: PASS
  regression_presence: PASS
  regression_rpc_lifecycle: PASS
  regression_method_codes: PASS
  regression_console_exec_cancel_timeout: PASS
  regression_stream_control: PASS
  declared_capabilities: PASS
  backward_compatibility: PASS
  mqtt_client_unchanged: PASS
  contract_digests_immutable: PASS
deployment_status: ACCEPTED
feature_flags:
  agent_contract_v1: enabled
  l4con_cmd_exec: enabled
  l4desk_desktop_stream: enabled
supersedes: []
known_risks:
  - "Interactive mouse/key input injection is rejected if screen is locked or session unavailable (L4D-INC-01)"
  - "Ctrl+Alt+Del requires shortcut_action ctrl_alt_del (L4D-INC-02)"
  - "Windows 7 SP1 requires TLS 1.2 KB3140245 (L4D-INC-03)"
  - "Exactly one concurrent video stream per terminal (L4D-INC-04)"
consumers:
  - L4D-17B-PB
next_prompt_id: L4D-17B-PB
```

---

## 1. Sequence gate (обязательный gate 16)

Все входные handoffs приняты в `l4desk-service/docs/prompts/contract-handoff.md`:

| Handoff | Статус | Принят |
|---|---|---|
| `H-L4D-16-MB-v1` | ACCEPTED | 2026-09-22T10:00:00Z |
| `H-L4D-01A-TOOLS-v1` | ACCEPTED | 2026-09-17T20:45:00Z |
| `H-L4D-01C-DOCS-v1` | ACCEPTED | 2026-09-17T22:05:00Z |

Ветка `l4desk/l4d-17a-tools` создана от `origin/l4desk/l4d-16-mb` @ `c4be01b`.

---

## 2. Опубликованный Agent artifact (без подмены)

Идентичность взята из `H-L4D-00A-TOOLS-v1` / `baseline_capabilities.json`, **не** из локальной сборки:

| Поле | Значение |
|---|---|
| Component | `l4tools` |
| Release Version | `1.7.7` |
| Git SHA | `297068f8044b5e40e51257e828c50d6137c9af7c` |
| SHA-256 (`l4setup.exe`) | `874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88` |
| Size | 28 730 368 bytes |
| Registry URL | `https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe` |
| Status | `PUBLISHED` |

**Substitution:** `false`. Локальные бинарники 1.8.x в acceptance не использованы и не коммитятся.

### Компонентный состав 1.7.7

| Компонент | Версия |
|---|---|
| leo4proxy | 1.2.0 |
| l4superv | 1.7.6 |
| l4desk | 1.7.6 |
| l4pin | 1.7.2 |
| l4con | 1.7.2 |
| l4sql | 1.0.0 |
| mosquitto | 2.1.2 |
| ffmpeg | 9.0 |

---

## 3. Contract digests (immutability vs H-L4D-01A-TOOLS-v1)

| Файл | SHA-256 | Match |
|---|---|---|
| `contracts/agent_compatibility_contract_v1.json` | `38e4ae5f13d563b3ae57a83259d63f9a63049d528d9667ae33efbd5a1e71267a` | YES |
| `contracts/schemas/agent_contract_v1.schema.json` | `f065dd53101c4bf90b237c85082a05eea212e1a2b3de39d2bf890708e3a42f1d` | YES |
| `contracts/schemas/presence_event.schema.json` | `fd061b7a1a113ec4ba518208e5693ffad98593cd8096d9a013f34a0ca7748d07` | YES |
| `contracts/schemas/rpc_7000_stream_control.schema.json` | `d7ed9deaeddc4a8a5f668cd59ac95aa0941aeff083e4ef6d94a1c9e75ba4ab6e` | YES |
| `contracts/schemas/rpc_7001_exec.schema.json` | `fb511cfb368f809fb042dbece552d7f24aeedb384f7186915f82e1dfb561ee1b` | YES |
| `contracts/schemas/rpc_7002_cancel.schema.json` | `a1dac309f324b1d0e2073ac8ab251ed70f6cfd191c46ae38f3c9192854079a8f` | YES |
| `contracts/schemas/l4rtp_wire_protocol.schema.json` | `b38aeeda96a5df81561117a0acb13606b417f136abb5250e72d41d38b630bd53` | YES |
| `fixtures/golden_vectors_v1.json` | `b4f3c1a465e88babf89c0850cfd8dffc29a4cd921ec51a73e2112e6cf9c3034b` | YES |
| `tests/test_agent_compatibility_contract_v1.py` | `7e6ce52fc9351cc8a95134bcde0304fc208e0fdbec3287f279b2a7bfd0db0d52` | YES |

Расхождений нет. Контракт не изменялся.

---

## 4. Golden fixtures (25 vectors)

Команда:

```text
cd tools
uv run pytest tests/test_agent_compatibility_contract_v1.py -v
```

Результат: **14/14 PASSED** (4.70 s), Python 3.14.0, pytest 9.1.1.

| Набор | Vectors | Тест | Verdict |
|---|---|---|---|
| `presence_vectors` | 6 | `test_presence_golden_vectors` + baseline backward-compat | PASS |
| `method_7000_vectors` | 10 | `test_method_7000_golden_vectors` + baseline backward-compat | PASS |
| `method_7001_vectors` | 4 | `test_method_7001_golden_vectors` + baseline backward-compat | PASS |
| `method_7002_vectors` | 2 | `test_method_7002_golden_vectors` + baseline backward-compat | PASS |
| `l4rtp_wire_vectors` | 3 | preamble + frame headers | PASS |

Дополнительно (в том же suite):

- `test_contract_spec_meta_schema` — PASS
- `test_contract_spec_methods_and_topics` — PASS
- `test_no_financial_fields_in_contract_and_fixtures` — PASS
- `test_all_topics_have_strict_device_or_server_prefix` — PASS

Итого golden vectors: **25/25**.

---

## 5. Regression suites

### 5.1 Certificate bootstrap — `tools/l4pin`

Команда: `tools\l4pin\build.cmd test` (MSVC `/MT`, x86 + x64, in-memory stores)

| Test | x86 | x64 |
|---|---|---|
| `test_absent` | PASS | PASS |
| `test_valid` | PASS | PASS |
| `test_expiring` | PASS | PASS |
| `test_broken_no_key` | PASS | PASS |
| `test_broken_wrong_sn` | PASS | PASS |
| `test_duplicates` | PASS | PASS |
| `test_non_leo4_issuer` | PASS | PASS |

**7/7 × 2 архитектуры.**

### 5.2 Presence / RPC lifecycle / stream control — `tools/l4desk`

Команда: `tools\l4desk\tests\run_tests.cmd` (MSVC x64)

**`test_ctl_protocol.exe` (presence payloads, 7000 commands, input gate):**

| Test | Verdict |
|---|---|
| `test_json_min` | PASS |
| `test_fnv1a_stability` | PASS |
| `test_mouse_mapping` | PASS |
| `test_keyboard_whitelist` | PASS |
| `test_protocol_payloads` | PASS |
| `test_command_handling_validation` | PASS |
| `test_input_gate_and_shortcuts` | PASS |

**`test_orchestrator.exe` (RPC lifecycle / stream control):**

| # | Сценарий | Verdict |
|---|---|---|
| 1 | stream_start (desktop) | PASS |
| 2 | already_running idempotency | PASS |
| 3 | controlled switch to camera | PASS |
| 4 | two-phase soft stop | PASS |
| 5 | already_stopped idempotency | PASS |
| 6 | hard kill phase 2 | PASS |
| 7 | crash detection | PASS |
| 8 | reconciliation / PID reuse | PASS |
| 9 | recovery-loop auto-restart | PASS |
| 10 | restart budget limit | PASS |
| 11 | restart cancel on stream_stop | PASS |
| 12 | lease watchdog fail-closed | PASS |
| 13 | input_release_all | PASS |

**`test_l4capture_adapter.exe` (stream/adapter/input gate):** **20/20 PASS**

Ключевые: `test_adapter_input_gate_low_480p_permitted`, `test_adapter_input_gate_default_strictly_denied`, `test_adapter_lease_renew_and_dedup`, `test_adapter_recovery_loop_and_backoff_cancel`, `test_adapter_session0_rejection`.

### 5.3 Console exec / result / cancel / timeout

Покрытие:

- golden vectors `method_7001` (streaming chunks `seq`/`eof`, `status: completed`)
- golden vectors `method_7002` (`cancelled`, `not_found`, `already_finished`)
- schema `rpc_7001_exec.schema.json` / `rpc_7002_cancel.schema.json` (timeout enforcement, exit codes)
- `test_command_handling_validation` (reject foreign SN / unsupported command / invalid command_id)
- `test_input_gate_and_shortcuts` (forbidden keys, invalid buttons)

**PASS.**

### 5.4 L4RTP/1 wire — `tools/leo4proxy`

Команда: `tools\leo4proxy\bin\test_rtp_wire.exe`

| Check | Verdict |
|---|---|
| Preamble byte-for-byte | PASS |
| Frame headers RTP/RTCP | PASS |
| Empty SN rejected | PASS |
| Identical RTP/RTCP ports rejected | PASS |
| Socket bind loopback + SO_RCVBUF ≥ 512KB | PASS |
| Tunnel stop clean | PASS |

**ALL RTP TUNNEL TESTS PASSED.**

---

## 6. Declared version / capabilities / backward compatibility

Источник: `fixtures/baseline_capabilities.json` + `contracts/agent_compatibility_contract_v1.json`.

| Проверка | Результат |
|---|---|
| `published_agent_versions` | `["1.7.6", "1.7.7"]` |
| Min supported | `1.7.6` |
| Method codes 7000/7001/7002 immutable | declared + golden-tested |
| Topic prefixes `dev/{SN}/`, `srv/{SN}/` | enforced by `test_all_topics_have_strict_device_or_server_prefix` |
| No financial fields | enforced by `test_no_financial_fields_in_contract_and_fixtures` |
| LWT/retain presence | enforced by presence golden vectors |
| Single stream / lease / locked-desktop constraints | CAP-01…CAP-06 + L4D-INC-01…04 recorded |

**Backward compatibility with 1.7.6: PASS** (contract covers both published releases; no breaking changes).

---

## 7. Инварианты

| Инвариант | Статус |
|---|---|
| Не подменять опубликованный Agent локальной сборкой | соблюдён (`substitution_used: false`) |
| Не менять MQTT wire contract / MQTT client | соблюдён (`git diff` по tracked файлам пуст) |
| Не публиковать несовместимый protocol | соблюдён (изменений contract/fixtures нет) |
| Defect → corrective, не скрытая замена | дефектов не обнаружено |

---

## 8. Воспроизводимые команды

```powershell
# 0. База
git switch l4desk/l4d-17a-tools   # HEAD = c4be01b

# 1. Golden fixtures + contract invariants (25 vectors)
cd tools
uv run pytest tests/test_agent_compatibility_contract_v1.py -v

# 2. Certificate bootstrap
cd tools\l4pin
build.cmd test

# 3. Presence / RPC lifecycle / stream control / adapter
cd tools\l4desk
tests\run_tests.cmd

# 4. L4RTP/1 wire
.\bin\test_rtp_wire.exe

# 5. Digests (сверка с H-L4D-01A-TOOLS-v1)
# см. §3
```

---

## 9. Compatibility matrix (summary)

| Consumer | Contract | Agent 1.7.6 | Agent 1.7.7 | Verdict |
|---|---|---|---|---|
| `iot-rpc-rest-app` (AgentContractV1Adapter) | Agent Compatibility Contract v1 | compatible | compatible | PASS |
| `l4desk` agent (methods 7000/7001/7002) | golden vectors | compatible | compatible | PASS |
| `l4con` (7001 exec / 7002 cancel) | golden vectors | compatible | compatible | PASS |
| `leo4proxy` (L4RTP/1) | wire fixtures | compatible | compatible | PASS |
| `l4pin` (certificate bootstrap) | cert discovery suite | compatible | compatible | PASS |

---

## 10. Вердикт

**`ACCEPTED`** — все checks зелёные.

- Exact artifact: `l4tools 1.7.7`, SHA-256 `874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88`
- Registry: `https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe`
- Consumer: `L4D-17B-PB`

Deploy отсутствует. Publish = данный отчёт + неизмененное artifact evidence.
