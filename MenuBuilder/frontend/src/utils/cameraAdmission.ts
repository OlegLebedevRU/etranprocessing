import { cameraEnrollment, cameraReadiness, prepareCamera, type CameraPreparation } from "../api/cameras";
import type { CameraAttempt } from "./cameraAttempt";

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
