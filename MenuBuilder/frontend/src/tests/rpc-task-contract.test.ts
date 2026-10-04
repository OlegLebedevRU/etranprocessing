import { describe, expect, it } from "vitest";
import { normalizeTask, maskTaskData, type TaskItem } from "../api/devices";
import { getMethodDefinition } from "../routes/devices/domain/methodCodes";

describe("actual IoT list/detail boundary", () => {
  for (const method of [50, 7001, 7002, 7011, 7999]) it(`normalizes nested header ${method}`, () => {
    const actual = { id: "fixture", created_at: 1, status: 3,
      header: { device_id: 773, method_code: method, ext_task_id: "fixture", priority: 0, ttl: 1 },
      results: [{ id: 1, ext_id: 0, status_code: 200, result: { dt: [{ pin: "000000" }] } }] };
    const task = normalizeTask(actual as unknown as TaskItem);
    expect(task.method_code).toBe(method); expect(task.device_id).toBe(773);
    expect(JSON.stringify(task)).not.toContain("000000");
  });
  it("redacts nested credentials and names native methods", () => {
    expect(maskTaskData({ dt: [{ PIN: "000000", nested: { token: "fixture-secret" } }] }))
      .toEqual({ dt: [{ PIN: "***", nested: { token: "***" } }] });
    for (const code of [7001, 7002, 7003, 7011]) expect(getMethodDefinition(code)?.label).toContain(String(code));
  });
  it("masks manual l4pin command arguments in history", () => {
    const masked = maskTaskData({ dt: [{ command_line: "l4pin --renew-authenticated --pin 000000" }] });
    expect(JSON.stringify(masked)).not.toContain("000000");
    expect(maskTaskData({ command_line: "echo fixture" })).toEqual({ command_line: "echo fixture" });
  });
});
