import { describe, expect, it } from "vitest";
import { cameraErrorMessage } from "../utils/cameraErrors";

describe("camera refusals", () => {
  it("explains a disabled server writer rather than suggesting unsupported hardware", () => {
    expect(cameraErrorMessage({ response: { status: 409, data: { detail: { code: "camera_writers_disabled", detail: "camera_writers_disabled" } } } })).toContain("выключено на сервере");
    expect(cameraErrorMessage({ response: { data: { detail: "ircut_method_unsupported" } } })).toContain("не поддерживается этой прошивкой");
  });
  it("does not expose a provider payload or credential material on validation failure", () => {
    expect(cameraErrorMessage({ response: { status: 422, data: { detail: [{ input: { password: "private-fixture-secret" } }] } } })).toBe("Камера или сервер не поддерживает выбранный параметр.");
  });
  it("retains a specific safe denial and an unconfirmed transport result", () => {
    expect(cameraErrorMessage({ response: { data: { detail: { message: "Сначала остановите просмотр" } } } })).toBe("Сначала остановите просмотр");
    expect(cameraErrorMessage(null)).toContain("Автоматического повтора нет");
  });
});
