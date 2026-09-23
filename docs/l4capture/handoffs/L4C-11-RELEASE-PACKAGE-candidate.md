<!-- HANDOFF:H-L4C-11-v1:BEGIN -->
```yaml
handoff_id: H-L4C-11-v1
status: ACCEPTED
contract_kinds:
  - TOOLS_SUITE_RELEASE_1_8_0
  - L4CAPTURE_PACKAGED
  - CONSUMER_METRICS_PLEN30_FIXED
  - RELEASE_COMPLETENESS_GATE
  - PUBLISHED_TO_GENERIC_REGISTRY
  - DOWNLOAD_SHA256_VERIFIED
producer_prompt_id: L4C-11-RELEASE-PACKAGE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-11-RELEASE-PACKAGE-report.md
producer_branch: l4capture/l4c-11-release-package
producer_commit: cd1f58f78d937f540594f67464cf094e7de88a58
accepted_at_utc: 2026-09-23T16:18:35Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.8.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-11-RELEASE-PACKAGE-report.md
  - tools/dist/l4setup.exe
  - tools/dist/l4tools-release.json
  - tools/dist/SHA256SUMS
  - tools/l4capture/evidence/1.8.0-download/l4setup.exe
  - tools/l4capture/evidence/1.8.0-download/SHA256SUMS
  - tools/l4capture/evidence/1.8.0-download/l4tools-release.json
  - tools/l4capture/SBOM.json
  - tools/l4capture/OPENH264_LICENSE.txt
  - tools/l4capture/ROLLBACK.md
  - tools/release/Test-L4CapturePackage.ps1
  - artifacts/l4tools/1.8.0.json
artifact_sha256:
  - 7ec4379f4a6a68bcc3a5733ae7828b34b5d5330203280a45aeeb90bfb6c3fac0
  - 4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8
  - 0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c
  - c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50
  - 4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8
  - c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50
  - 0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c
  - 69e8fa05ffcf4e391774ce6211343abb6b797985cd6ed59cc56cc2b0ce3ebd00
  - 1ca9a98d1b5c6d6f0411c6cb8732299670fafbc33b92fedeca8a86b1e4d3d72e
  - d2006e44efd8332b325eb6faefa10052f55e005fdc72fbad86e0d8012e01773a
  - 66f2b4960f1211d3b50fd478bac0dcbf2c14eb2d65e0d1407f58c2534ca333aa
  - 260187c2d37f5ca98ade068dd0406cc00330ad202030130c5bf1ccad4f677553
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
    - H-L4C-08-v1
    - H-L4C-09-v1
    - H-L4C-10-v1
  breaking_changes: false
  notes: "Tools suite 1.8.0 published to Generic Artifact Registry and download-verified (3/3 SHA-256). l4capture 1.0.0 in both payloads at l4capture\\bin. Consumer EVENT_METRICS gdi_handles plen>=30 fixed. Owner 2026-09-23: commit release-scope only; G4 Win7 matrix and G10 2h soak deferred to post-release; --allow-dirty override for publisher. legal_review OUT_OF_SCOPE_BY_OWNER_DECISION. production_deployed=false."
deployment_status: PUBLISHED_TO_REGISTRY
deployed_environment: generic_artifact_registry_l4tools_1.8.0_local_build_win10_x64
feature_flags:
  media_backend: l4capture
  media_backend_fallback: ffmpeg
contract_payload:
  release:
    suite_version: "1.8.0"
    artifact_version: "1.8.0"
    min_os: "6.1"
    arch: [x86, x64]
    signed: false
    source_git_sha: cd1f58f78d937f540594f67464cf094e7de88a58
    source_dirty: true
    dirty_override: owner_allow_dirty_2026-09-23
    production_deployed: false
    legal_review: OUT_OF_SCOPE_BY_OWNER_DECISION
  install_paths:
    l4capture: C:\l4tools\l4capture\bin\l4capture.exe
    adapter_lookup: "%BASE%\\l4capture\\bin"
    guide: term_tool-user-guide.md
  components:
    leo4proxy: "1.2.0"
    l4superv: "1.7.6"
    l4desk: "1.7.6"
    l4pin: "1.7.2"
    l4con: "1.7.2"
    l4sql: "1.0.0"
    l4capture: "1.0.0"
    mosquitto: "2.1.2"
    ffmpeg: "9.0"
  registry:
    published: true
    published_at_utc: "2026-09-23T16:18:35Z"
    urls:
      - https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/l4setup.exe
      - https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/SHA256SUMS
      - https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/l4tools-release.json
    downloaded_hashes:
      l4setup.exe: 4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8
      SHA256SUMS: c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50
      l4tools-release.json: 0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c
    download_verified: true
    registry_digest_verified: true
    audit: artifacts/l4tools/1.8.0.json
  gate_evidence:
    G1: OVERRIDDEN_DIRTY_OWNER
    G2: PASS
    G3: PASS
    G4: POST_RELEASE
    G5: POST_RELEASE
    G6: PARTIAL
    G7: PARTIAL
    G8: POST_RELEASE
    G9: PARTIAL
    G10: POST_RELEASE
    G11: PASS
supersedes: []
known_risks:
  - G4 Win7/Embedded matrix and G10 2h soak deferred post-release by owner 2026-09-23.
  - G5/G8 install-rollback and delivery matrix deferred post-release.
  - source dirty=true (foreign MenuBuilder/l4media/l4desk-service worktree); published with owner --allow-dirty.
  - Authenticode NotSigned (Stage 1 debt).
  - production_deployed=false; no mass upgrade / latest channel change.
consumers:
  - TOOLS_SUITE_RELEASE
  - TERMINAL_OPERATIONS
next_prompt_id: null
```
<!-- HANDOFF:H-L4C-11-v1:END -->
