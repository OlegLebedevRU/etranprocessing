export type SaveHandle = { createWritable(): Promise<FileSystemWritableFileStream> };
type PickerWindow = Window & { showSaveFilePicker?: (options: { suggestedName: string }) => Promise<SaveHandle> };

// Call directly from the click handler, before any network await consumes activation.
export async function chooseDownloadTarget(name: string): Promise<SaveHandle | undefined | null> {
  const picker = (window as PickerWindow).showSaveFilePicker;
  if (!picker) return undefined;
  try {
    return await picker.call(window, { suggestedName: name });
  } catch (error) {
    // AbortError also covers browser rejection of a sensitive target: do not bypass it.
    if (error instanceof DOMException && error.name === "AbortError") return null;
    if (error instanceof DOMException && ["SecurityError", "NotAllowedError", "NotSupportedError"].includes(error.name)) return undefined;
    throw error;
  }
}

export async function saveVerifiedDownload(blob: Blob, name: string, handle: SaveHandle | undefined, signal: AbortSignal): Promise<void> {
  signal.throwIfAborted();
  if (handle) {
    const writable = await handle.createWritable();
    try {
      signal.throwIfAborted();
      await writable.write(blob);
      signal.throwIfAborted();
      await writable.close();
    } catch (error) {
      // A failed write/close must not silently download another copy elsewhere.
      try { await writable.abort(); } catch { /* The stream may already be closed/errored. */ }
      throw error;
    }
    return;
  }
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url; link.download = name;
  document.body.appendChild(link);
  try { link.click(); } finally { link.remove(); setTimeout(() => URL.revokeObjectURL(url), 10000); }
}
