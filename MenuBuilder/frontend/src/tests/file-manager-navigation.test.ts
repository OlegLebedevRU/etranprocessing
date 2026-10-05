import { describe, expect, it } from "vitest";
import { fmJoin, fmParent, fmWithinRoot } from "../routes/files/navigation";
describe("FM Explorer path boundaries", () => {
  it("returns from a drive to the terminal, never asks the agent for C:", () => {
    expect(fmParent("C:\\", ["C:\\"])).toBe("");
    expect(fmParent("C:\\Reports", ["C:\\"])).toBe("C:\\");
    expect(fmJoin("C:\\", "Reports")).toBe("C:\\Reports");
  });
  it("respects old-agent bounded roots and rejects sibling prefix escapes", () => {
    const roots = ["C:\\l4tools\\fm"];
    expect(fmParent(roots[0], roots)).toBe("");
    expect(fmWithinRoot("c:\\L4tools\\FM\\report.txt", roots)).toBe(true);
    for (const path of ["C:\\l4tools", "C:\\l4tools\\fm-other", "C:\\l4tools\\fm\\..\\outside", "C:relative", "\\\\server\\share"]) expect(fmWithinRoot(path, roots)).toBe(false);
  });
});
