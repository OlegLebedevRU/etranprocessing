#ifndef L4C_MF_ENCODER_H
#define L4C_MF_ENCODER_H

#include "encoder_backend.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Проверка доступности и работоспособности аппаратного H.264 MFT энкодера.
 * Выполняет безопасное зондирование (probe) за время <= 2.0 секунд.
 * Возвращает true только при наличии подтверждённого аппаратного энкодера GPU
 * (Intel QSV, NVIDIA NVENC, AMD AMF), поддерживающего входной формат NV12
 * и профиль H.264 Constrained Baseline / Level 3.1.
 * Возвращает false на Windows 7, виртуальных машинах без GPU или при сбое MFT.
 */
bool l4c_mf_encoder_is_supported(void);

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
