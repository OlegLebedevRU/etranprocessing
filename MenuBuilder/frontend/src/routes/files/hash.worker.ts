import { sha256 } from "@noble/hashes/sha2.js";

self.onmessage = async ({ data }: MessageEvent<{ file: Blob }>) => {
  try {
    const hash = sha256.create();
    for (let offset = 0; offset < data.file.size; offset += 1024 * 1024) {
      hash.update(new Uint8Array(await data.file.slice(offset, offset + 1024 * 1024).arrayBuffer()));
    }
    self.postMessage({ sha256: Array.from(hash.digest(), b => b.toString(16).padStart(2,"0")).join("") });
  } catch { self.postMessage({ error: true }); }
};
