import { describe, expect, it } from "vitest";
import { hasPermission, PERMISSION_VIDEO_VIEW } from "../utils/permissions";

describe("video permission for L4Desk owner", () => {
  it("allows role 5 to fetch the video status, matching the backend", () => {
    expect(
      hasPermission(
        { username: "owner", org_id: 1000, role_id: 5, permissions: [] },
        PERMISSION_VIDEO_VIEW
      )
    ).toBe(true);
  });

  it("keeps viewer access tied to the explicit permission", () => {
    expect(hasPermission({ username: "viewer", role_id: 4, permissions: [] }, PERMISSION_VIDEO_VIEW)).toBe(false);
    expect(
      hasPermission(
        { username: "viewer", role_id: 4, permissions: [PERMISSION_VIDEO_VIEW] },
        PERMISSION_VIDEO_VIEW
      )
    ).toBe(true);
  });
});
