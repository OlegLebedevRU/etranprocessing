# 17F MenuBuilder corrective evidence — provenance

Verification status: `VERIFIED` for five copied Markdown artifacts and
one finite derived registration extract.
This is a data-only export for `L4D-17F-DOCS-FIX-01`. It does not accept
the runtime behavior or replace the final 17F black-box decision.

| Export | Source path | Source commit | Git/raw SHA-256 |
|---|---|---|---|
| `grace.md` | `MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-report.md` | `7c510b8d9bf79b45c66ccdc05102f72efe73cb2a` | `1c1c1d7ec8d7105043723b5e300641dbf12249d8242af0440b3182fd4bc628a6` |
| `payment-recovery.md` | `MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-PAYMENT-RECOVERY-01-report.md` | `b5d0e2a5b90188d8c0964dd3423fb8230ee21007` | `8f8d30d661456be5081ac146ea27af6f8d6f9fbcbe64df158b723e16c02618dd` |
| `archive-consumer.md` | `MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-ARCHIVE-CONSUMER-EVIDENCE-01-report.md` | `db6a24a5f22015e8a35525cc798301c6f74dc889` | `2f497f0c72a3d79a6d33825bcaa945bec603faaa80ba99269828821ae0b6d4ad` |
| `media-soak-extract.md` | `.agent-context/tasks/active/l4d-main-convergence.md`, lines 68–87 only | `44ac16e3f302be00b60636ea9fe42f5dec63534c` | `814f6cd3f6ca5bc6d2b04a5f587efc782e718f57d779d5e101c2358c4259570b` |
| `agent-release-baseline.md` | `.agent-context/tasks/l4tools-1.8.2-beta-1-handoff.md` | `41b73bc98bec0013409a77c3a7551e317ca4b086` | `61c6df8716ce735a92c4ccd17e0436e2bceebddfc1db4aa2f830652c73b7db98` |
| `registration-evidence.md` | Accepted 17E R3 plus `.agent-context/tasks/active/l4d-17e-org-id-handoff.md`; finite summary, not a raw copy | `f031b963f43d9c52fbf28aee9f978157386721f1` and `3c5113cefd8d96804b30d489d07cf40606b6b32b` | Export SHA-256 `1831592b319099525b25240c79c576618b0667c0be7c3ee7aa5c364e5c0e52f6`; source SHA-256 values are in the extract |

The controller copied each source with `git show <commit>:<path>` as raw
bytes and compared SHA-256 of source and export. For the media-soak extract,
the copied bytes are exactly the 1-based lines 68–87 of the Git/raw source;
the full source SHA-256 is
`bdeac3d74b9ec8477c4a5f64eaee39bf9600c48658faeb1851a01d3f95650b96`.
The source commits
were published on `release/l4tools-1.8.2-beta-1` before this export.
No MenuBuilder source code, database, server file, or runtime setting was
changed for this documentation step.

Evidence limits remain those in the reports: grace and payment behavior
were exercised in the isolated test backend; the payment recovery code
had not been rolled into production at the time of its report. The archive
consumer used an in-memory database double and skipped volume availability;
it is a producer-to-consumer contract check, not a production Hub import.
The earlier release-baseline handoff said a two-hour soak had not yet run;
the later dated media-soak extract records session 462 for 7,492 seconds
and the subsequent 61-second session 465. These are historical, operator
observations, not a new test performed by this export. The user's release
decision accepts Agent `1.8.2-beta-1` as-is without requiring a rebuild.
The accepted 14-MB contract defines uniqueness for monthly charges and
payment postings, along with balanced entries. A repeated reconciliation
operation ID that creates a second audit run is therefore not, by itself,
evidence of duplicate financial posting. The final 17F report should
check the actual ledger and classify the extra audit run separately.
17F must combine these data-only reports with its own current deployment
and black-box evidence before issuing a final verdict.
