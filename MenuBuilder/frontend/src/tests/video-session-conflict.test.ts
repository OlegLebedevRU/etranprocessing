import { describe, expect, it } from "vitest";
import { isVideoSessionConflict, videoErrorMessage } from "../utils/videoErrors";
import { classifyRefusalReason } from "../components/RefusalReasonCard";

describe("video session conflicts", () => {
  it("does not misrepresent an inbox or freshness failure as another operator", () => {
    for (const code of ["camera_inbox_busy", "camera_main_app_or_gauge_not_fresh"]) {
      expect(isVideoSessionConflict(code)).toBe(false);
      expect(videoErrorMessage(code)).toBeTruthy();
      expect(classifyRefusalReason(code).type).not.toBe("session_conflict");
    }
    expect(isVideoSessionConflict("Conflict")).toBe(false);
  });
  it("preserves actual stream and lease conflicts", () => {
    for (const code of ["camera_busy_stream", "session_busy", "lease_taken"]) {
      expect(isVideoSessionConflict(code)).toBe(true);
    }
  });
});
