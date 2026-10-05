import { afterEach, expect, it, vi } from "vitest";
import { chooseDownloadTarget, saveVerifiedDownload } from "../routes/files/saveDownload";

afterEach(() => vi.unstubAllGlobals());
it("opens picker immediately with suggested name and preserves chosen handle", async () => {
  const handle = { createWritable: vi.fn() };
  const picker = vi.fn().mockResolvedValue(handle);
  vi.stubGlobal("window", { showSaveFilePicker: picker });
  const result = chooseDownloadTarget("report.txt");
  expect(picker).toHaveBeenCalledWith({ suggestedName: "report.txt" });
  expect(await result).toBe(handle);
});
it.each(["SecurityError", "NotAllowedError", "NotSupportedError"])("falls back when picker is blocked: %s", async name => {
  vi.stubGlobal("window", { showSaveFilePicker: () => Promise.reject(new DOMException("Blocked", name)) });
  expect(await chooseDownloadTarget("report.txt")).toBeUndefined();
});
it("unsupported browser falls back, cancellation does not", async () => {
  vi.stubGlobal("window", {});
  expect(await chooseDownloadTarget("report.txt")).toBeUndefined();
  vi.stubGlobal("window", { showSaveFilePicker: () => Promise.reject(new DOMException("Cancelled", "AbortError")) });
  expect(await chooseDownloadTarget("report.txt")).toBeNull();
});
it("writes verified blob and awaits close", async () => {
  const stream = { write: vi.fn(), close: vi.fn(), abort: vi.fn() };
  const createWritable = vi.fn().mockResolvedValue(stream);
  const blob = new Blob(["verified"]);
  await saveVerifiedDownload(blob, "report.txt", { createWritable }, new AbortController().signal);
  expect(stream.write).toHaveBeenCalledWith(blob);
  expect(stream.close).toHaveBeenCalledOnce();
  expect(stream.abort).not.toHaveBeenCalled();
});
it("failed disk write aborts and never silently switches to browser download", async () => {
  const failure = new Error("disk full");
  const stream = { write: vi.fn().mockRejectedValue(failure), close: vi.fn(), abort: vi.fn() };
  await expect(saveVerifiedDownload(new Blob(), "report.txt", { createWritable: vi.fn().mockResolvedValue(stream) }, new AbortController().signal)).rejects.toBe(failure);
  expect(stream.abort).toHaveBeenCalledOnce();
  expect(stream.close).not.toHaveBeenCalled();
});
it("cancellation while opening destination prevents writing", async () => {
  const controller = new AbortController();
  const stream = { write: vi.fn(), close: vi.fn(), abort: vi.fn() };
  const createWritable = vi.fn(async () => { controller.abort(); return stream as unknown as FileSystemWritableFileStream; });
  await expect(saveVerifiedDownload(new Blob(), "report.txt", { createWritable }, controller.signal)).rejects.toThrow();
  expect(stream.write).not.toHaveBeenCalled();
  expect(stream.abort).toHaveBeenCalledOnce();
});
