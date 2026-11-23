import { cameraEnrollment, cameraReadiness, commandCamera, getCamera, getCameraCommand, prepareCamera, type CameraPreparation } from "../api/cameras";
import type { CameraAttempt } from "./cameraAttempt";

export async function readCameraForStart(device: number, attempt: CameraAttempt) {
  const observation = await attempt.wait(getCamera(device, attempt.signal));
  const snapshot = observation.snapshot;
  const received = Date.parse(snapshot?.received_at ?? "");
  const age = Date.now() - received;
  // IoT media v3 admits observations for at most 30 s, even for a 330 s config gauge.
  const limit = Math.min(snapshot?.freshness_max_age_sec ?? 330, 30);
  const previous = snapshot?.cameras.find(camera => camera.camera_id === "cam:ip000001");
  const selected = previous?.discovery?.candidates.filter(camera => camera.port_verified && camera.auth === "verified") ?? [];
  if (selected.length > 1) throw new Error("Выберите одну камеру на port-2");
  if (selected.length === 1 && previous?.discovery?.state === "ready" && !previous.stale &&
      snapshot?.media_observations?.some(item => item.source_id === previous.camera_id && item.actual_track &&
        (!item.physical_camera_mac || item.physical_camera_mac === selected[0].mac)) &&
      age >= 0 && age < limit * 1000 - 2000) return observation;
  if (observation.lease_active !== false || !observation.terminal_online)
    throw new Error("Для проверки камеры необходим терминал на связи со свободной арендой");

  let check = await runCameraCheck(device, "refresh", attempt);
  const camera = check.observation.snapshot?.cameras.find(item => item.camera_id === "cam:ip000001");
  const missing = check.error ? ["camera_unavailable", "camera_not_resolved"].includes(check.error) :
    camera?.discovery?.state === "not_found" || camera?.discovery?.state === "link_down";
  if (missing && camera?.capabilities?.methods?.includes(165) && check.observation.lease_active === false) {
    check = await runCameraCheck(device, "discover", attempt);
    if (!check.error && check.observation.snapshot?.cameras.some(item => item.discovery?.state === "resolving_ip"))
      check = await runCameraCheck(device, "refresh", attempt);
  }
  if (check.error) throw new Error(`Терминал не подтвердил состояние камеры (${check.error})`);
  const found = check.observation.snapshot?.cameras.find(item => item.camera_id === "cam:ip000001");
  if (selected.length && !found?.discovery?.candidates.some(item =>
    item.mac === selected[0].mac && item.port_verified && item.auth === "verified"))
    throw new Error("Камера на port-2 изменилась. Проверьте подключение перед запуском");
  if (found?.discovery?.state === "ready" && found.discovery.candidates.some(item => item.port_verified && item.auth === "verified") &&
      !check.observation.snapshot?.media_observations?.some(item => item.source_id === found.camera_id && item.actual_track))
    throw new Error("Камера на port-2 ответила, но терминал не подтвердил видеодорожку RTSP за время проверки");
  return check.observation;
}

async function runCameraCheck(device: number, action: "refresh" | "discover", attempt: CameraAttempt) {
  const task = await attempt.wait(commandCamera(device, { action, operation_id: crypto.randomUUID(),
    ...(action === "discover" ? { allow_disruptive: true } : {}) }));
  const deadline = performance.now() + (action === "discover" ? 40000 : 25000);
  let snapshotId: string | undefined;
  let error: string | undefined;
  while (performance.now() < deadline) {
    if (!snapshotId) {
      const current = await attempt.wait(getCameraCommand(device, task.id, attempt.signal));
      const result = current.results?.[0];
      if (result) {
        const body = typeof result.result === "string" ? JSON.parse(result.result) : result.result;
        if (!body?.snapshot_id)
          throw new Error("Терминал не подтвердил состояние камеры");
        if (result.status_code < 200 || result.status_code >= 300)
          error = body.error || `HTTP ${result.status_code}`;
        snapshotId = body.snapshot_id;
      } else if ([4, 5, 6, 7].includes(current.status)) {
        throw new Error("Проверка камеры завершилась без результата");
      }
    }
    if (snapshotId) {
      const updated = await attempt.wait(getCamera(device, attempt.signal));
      if (updated.snapshot?.operation_id === task.id && updated.snapshot.snapshot_id === snapshotId) {
        return { observation: updated, error };
      }
    }
    await attempt.wait(new Promise(resolve => setTimeout(resolve, 1000)));
  }
  throw new Error("Терминал не успел подтвердить новый снимок камеры");
}

export async function admitCamera(device: number, selection: CameraPreparation, attempt: CameraAttempt,
  stage: (value: string) => void,
  api = { prepareCamera, cameraEnrollment, cameraReadiness }) {
  stage("Проверка камеры…");
  const deadline = performance.now() + 30000;
  const prepared = await attempt.wait(api.prepareCamera(device, selection, attempt.signal));
  stage("Подключение терминала…");
  while (true) {
    if (performance.now() >= deadline) throw new Error("Терминал не подтвердил подключение камеры вовремя. Проверьте обновление терминала.");
    const state = await attempt.wait(api.cameraEnrollment(device, prepared, attempt.signal));
    if (performance.now() >= deadline) throw new Error("Время подключения камеры истекло");
    if (state.enrolled) break;
    await attempt.wait(new Promise(resolve => setTimeout(resolve, 250)));
  }
  stage("Проверка готовности терминала…");
  return attempt.wait(api.cameraReadiness(device, prepared, attempt.signal));
}
