import axios from "axios";

interface Stats {
  groups: number;
  services: number;
  tsp_codes: number;
  avg_price: number;
}

export const getStats = () => axios.get<Stats>("/api/stats");
