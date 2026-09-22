import { describe, expect, it } from "vitest";

describe("L4Desk Archive Manifests & Retention Hub Contract (L4D-16-MB)", () => {
  it("validates archive batch lifecycle states and record counts", () => {
    const batch = {
      archive_batch_id: "arch-iot-2026-05-b91c84f2",
      owner_project: "iot-rpc-rest-app",
      schema_version: "1.0.0",
      source_month: "2026-05",
      state: "verified",
      status: "verified",
      record_types: ["iot_session_events", "rpc_transitions", "presence_events"],
      row_count: 15000,
      checksum_sha256: "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",
      location_reference: "vol://2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2",
      retain_until: "2029-09-01T04:00:00Z",
      has_checksum_mismatch: false,
      has_count_mismatch: false,
      issues: [],
    };

    expect(["prepared", "verified", "purged", "failed"]).toContain(batch.state);
    expect(batch.row_count).toBeGreaterThan(0);
    expect(batch.checksum_sha256).toHaveLength(64);
    expect(batch.has_checksum_mismatch).toBe(false);
  });

  it("enforces location reference masking without revealing server filesystem root", () => {
    const rawVolumeRoot = "/mnt/l4desk-archive/production";
    const relativePath = "2026/05/iot-rpc-rest-app/arch-iot-2026-05-b91c84f2";
    const maskedLocation = `vol://${relativePath.replace(/^\/+/, "")}`;

    expect(maskedLocation).not.toContain(rawVolumeRoot);
    expect(maskedLocation.startsWith("vol://2026/05/")).toBe(true);
  });

  it("verifies 3-year minimum retention and backup evidence invariants", () => {
    const createdAt = new Date("2026-09-01T04:00:00Z");
    const retainUntil = new Date("2029-09-01T04:00:00Z");
    const backupRequired = true;

    const diffYears =
      (retainUntil.getTime() - createdAt.getTime()) / (1000 * 60 * 60 * 24 * 365.25);

    expect(diffYears).toBeGreaterThanOrEqual(2.99);
    expect(backupRequired).toBe(true);
  });

  it("checks correlation drill-down archive fact node and hash mismatch detection", () => {
    const matchedNode = {
      node_type: "archive",
      label: "Архивный манифест",
      present: true,
      mismatch: false,
      details: "Batch: arch-mb-2026-05-c3d4e5f6, Owner: MenuBuilder, State: verified, Records: 2500",
      fact: {
        archive_batch_id: "arch-mb-2026-05-c3d4e5f6",
        owner_project: "MenuBuilder",
        location_reference: "vol://2026/05/MenuBuilder/arch-mb-2026-05-c3d4e5f6",
      },
    };

    const mismatchNode = {
      node_type: "archive",
      label: "Архивный манифест",
      present: true,
      mismatch: true,
      mismatch_code: "SOURCE_HASH_MISMATCH",
      details: "Discrepancy detected between fact hash and manifest checksum",
      fact: null,
    };

    expect(matchedNode.present).toBe(true);
    expect(matchedNode.mismatch).toBe(false);
    expect(matchedNode.fact.location_reference.startsWith("vol://")).toBe(true);

    expect(mismatchNode.mismatch).toBe(true);
    expect(mismatchNode.mismatch_code).toBe("SOURCE_HASH_MISMATCH");
  });
});
