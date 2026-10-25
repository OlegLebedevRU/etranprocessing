/** Stable admission reasons; never expose provider payloads or credentials. */
const videoErrors: Record<string, string> = {
  transport_not_configured: "На сервере не настроены порты видеотранспорта. Обратитесь к администратору.",
  disabled_by_policy: "Новый режим видео выключен на сервере. Для совместимого просмотра выберите H.264.",
  negotiation_unavailable: "Сервер не подтвердил готовность нового режима видео. Проверьте его настройки или выберите H.264.",
  legacy_h264_only: "Агент поддерживает прежний режим H.264. Выберите Auto или H.264.",
  capability_mismatch: "Версии или сведения о видеовозможностях не совпадают. Обновите сведения об устройстве.",
  invalid_video_contract: "Получен несовместимый ответ видеосервиса. Обратитесь к администратору.",
  stale_capability: "Сведения о видеовозможностях устарели. Обновите источники перед запуском.",
  encoder_busy: "Кодировщик занят, проверка H.265 отложена",
  probe_timeout: "Терминал не завершил проверку кодировщика вовремя. Проверьте его состояние перед повторным запуском.",
  no_hardware_mft: "Аппаратный кодировщик H.265 недоступен. Выберите H.264.",
  os_api_unavailable: "Система терминала не поддерживает новый кодировщик. Выберите H.264.",
  server_hevc_unavailable: "Медиасервис не подтвердил поддержку выбранного кодека.",
  server_unavailable: "Медиасервис недоступен. Проверьте соединение с сервером.",
  server_format_unsupported: "Медиасервис не поддерживает формат выбранного источника.",
  profile_unsupported: "Источник не поддерживает выбранное качество. Выберите другой профиль или H.264.",
  browser_codec_unsupported: "Браузер не подтвердил поддержку выбранного кодека. Попробуйте H.264 или другой браузер.",
  browser_capabilities_invalid: "Не удалось прочитать видеовозможности браузера. Обновите страницу.",
  browser_capabilities_required: "Для нового режима видео нужны сведения о возможностях браузера. Обновите страницу.",
};

export function videoErrorMessage(code?: string): string | undefined {
  return code && Object.prototype.hasOwnProperty.call(videoErrors, code) ? videoErrors[code] : undefined;
}
