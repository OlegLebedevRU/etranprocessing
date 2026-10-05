/** Windows paths are checked again by the server and the agent. UI bounds prevent accidental parent escape. */
export function fmJoin(parent: string, name: string): string {
  return `${parent.replace(/\\+$/, "")}\\${name}`;
}
export function fmWithinRoot(path: string, roots: string[]): boolean {
  if (!/^[a-z]:\\/i.test(path) || path.split("\\").some(part => part === "." || part === "..")) return false;
  const normalized = path.replace(/\\+$/, "").toLowerCase();
  return roots.some(root => {
    const boundary = root.replace(/\\+$/, "").toLowerCase();
    return normalized === boundary || normalized.startsWith(`${boundary}\\`);
  });
}
export function fmParent(path: string, roots: string[]): string {
  const normalized = path.replace(/\\+$/, "");
  if (roots.some(root => root.replace(/\\+$/, "").toLowerCase() === normalized.toLowerCase())) return "";
  const index = normalized.lastIndexOf("\\");
  const parent = index === 2 ? normalized.slice(0, 3) : normalized.slice(0, index);
  return fmWithinRoot(parent, roots) ? parent : "";
}
