#ifndef L4C_MF_ENCODER_H
#define L4C_MF_ENCODER_H

#include "encoder_backend.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Проверка доступности аппаратного H.264 MFT энкодера.
 *
 * Контракт: сохранённая однажды рабочая конфигурация (mft_capability.ini рядом
 * с exe) используется без повторной пробы. Probe выполняется только при cache
 * miss / retryable-сбое и ограничен бюджетом <= 5.0 с (fail-closed: true после
 * бюджета не возвращается). Результат probe персистентно сохраняется.
 * Runtime-отказ MFT инвалидирует кэш (следующий старт — редкая повторная проба),
 * штатный fallback на OpenH264 остаётся в main.
 */
bool l4c_mf_encoder_is_supported(void);

/* Сброс in-memory и on-disk кэша MFT (тесты / ручная пере-валидация). */
void l4c_mf_encoder_cache_reset(void);

/* Runtime-отказ MFT (init/encode DEVICE_LOST): пометить кэш retryable. */
void l4c_mf_encoder_note_runtime_failure(void);

/*
 * Фабричный метод создания экземпляра бэкенда аппаратного кодирования MF.
 * Выделяет структуру бэкенда и привязывает виртуальную таблицу функций.
 */
l4c_status_t l4c_mf_encoder_create(l4c_encoder_backend_t **out_backend);

/*
 * Тестовые хуки для валидации поведения при сбоях и таймаутах.
 */
void l4c_mf_encoder_test_inject_probe_fail(bool fail);
void l4c_mf_encoder_test_inject_process_fail(bool fail);

#ifdef __cplusplus
}
#endif

#endif /* L4C_MF_ENCODER_H */
