import axios from "axios";

const client = axios.create({
  baseURL: "/api",
  headers: { "Content-Type": "application/json" },
});

client.interceptors.request.use((config) => {
  const token = localStorage.getItem("mb_token");
  if (token) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

client.interceptors.response.use(
  (res) => res,
  (err) => {
    if (err.response?.status === 401) {
      localStorage.removeItem("mb_token");
      localStorage.removeItem("mb_user");
      if (window.location.pathname !== "/login") {
        window.location.href = "/login";
      }
    }
    const msg =
      err.response?.data?.detail || err.message || "Unknown error";
    return Promise.reject(new Error(msg));
  }
);

export default client;
