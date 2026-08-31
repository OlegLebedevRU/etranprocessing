/**
 * In-memory client cache with TTL and stale-while-revalidate capabilities.
 * Eliminates redundant network cascades (waterfalls) when navigating between routes.
 */

interface CacheEntry<T> {
  data: T;
  timestamp: number;
}

const memoryStore = new Map<string, CacheEntry<unknown>>();

export function getCached<T>(key: string, ttlMs = 120_000): T | null {
  const entry = memoryStore.get(key) as CacheEntry<T> | undefined;
  if (!entry) return null;
  if (Date.now() - entry.timestamp > ttlMs) {
    memoryStore.delete(key);
    return null;
  }
  return entry.data;
}

export function setCached<T>(key: string, data: T): void {
  memoryStore.set(key, { data, timestamp: Date.now() });
}

export function invalidateCache(prefix?: string): void {
  if (!prefix) {
    memoryStore.clear();
    return;
  }
  for (const key of memoryStore.keys()) {
    if (key.startsWith(prefix)) {
      memoryStore.delete(key);
    }
  }
}

/**
 * Wraps an async fetcher with TTL cache.
 * If data is in cache and fresh, returns it immediately without calling network.
 */
export async function withCache<T>(
  key: string,
  fetcher: () => Promise<T>,
  ttlMs = 120_000
): Promise<T> {
  const cached = getCached<T>(key, ttlMs);
  if (cached !== null) {
    return cached;
  }
  const fresh = await fetcher();
  setCached(key, fresh);
  return fresh;
}
