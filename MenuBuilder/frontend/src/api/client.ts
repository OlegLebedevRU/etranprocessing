import axios from "axios";
import { notifySessionEvent, scheduleRefresh } from "./session";

const authTransport =
  (import.meta as unknown as { env?: Record<string, string | undefined> }).env
    ?.VITE_AUTH_TRANSPORT || "cookie";

const client = axios.create({
  baseURL: "/api",
  headers: {
    "Content-Type": "application/json",
    "X-Requested-With": "XMLHttpRequest",
  },
  withCredentials: true,
});

client.interceptors.request.use((config) => {
  config.headers["X-Requested-With"] = "XMLHttpRequest";
  if (authTransport === "bearer") {
    const token = localStorage.getItem("mb_token");
    if (token) {
      config.headers.Authorization = `Bearer ${token}`;
    }
  }
  return config;
});

let isRefreshing = false;
let failedQueue: Array<{
  resolve: (value?: unknown) => void;
  reject: (reason?: unknown) => void;
}> = [];

const processQueue = (error: unknown, token: string | null = null) => {
  failedQueue.forEach((prom) => {
    if (error) {
      prom.reject(error);
    } else {
      prom.resolve(token);
    }
  });
  failedQueue = [];
};

client.interceptors.response.use(
  (res) => res,
  async (err) => {
    const originalRequest = err.config;

    if (
      err.response?.status === 401 &&
      !originalRequest._retry &&
      !originalRequest.url?.includes("/auth/login") &&
      !originalRequest.url?.includes("/auth/refresh")
    ) {
      if (isRefreshing) {
        return new Promise((resolve, reject) => {
          failedQueue.push({ resolve, reject });
        })
          .then((token) => {
            if (token) {
              originalRequest.headers.Authorization = `Bearer ${token}`;
            }
            return client(originalRequest);
          })
          .catch((err) => Promise.reject(err));
      }

      originalRequest._retry = true;
      isRefreshing = true;

      try {
        const { data } = await axios.post(
          "/api/auth/refresh",
          {},
          {
            withCredentials: true,
            headers: { "X-Requested-With": "XMLHttpRequest" },
          }
        );
        const newToken = data.access_token;
        if (authTransport === "bearer" && newToken) {
          localStorage.setItem("mb_token", newToken);
          client.defaults.headers.common.Authorization = `Bearer ${newToken}`;
          originalRequest.headers.Authorization = `Bearer ${newToken}`;
        }
        processQueue(null, newToken);
        if (data.expires_in) {
          scheduleRefresh(data.expires_in);
          notifySessionEvent({
            type: "token-refreshed",
            expiresAt: Date.now() + data.expires_in * 1000,
          });
        }
        return client(originalRequest);
      } catch (refreshErr) {
        processQueue(refreshErr, null);
        localStorage.removeItem("mb_token");
        localStorage.removeItem("mb_user");
        notifySessionEvent({ type: "logout" });
        if (window.location.pathname !== "/login") {
          window.location.href = "/login";
        }
        return Promise.reject(refreshErr);
      } finally {
        isRefreshing = false;
      }
    }

    const msg =
      err.response?.data?.detail || err.message || "Unknown error";
    return Promise.reject(new Error(msg));
  }
);

export default client;
