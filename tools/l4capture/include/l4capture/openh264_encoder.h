#ifndef L4C_OPENH264_ENCODER_H
#define L4C_OPENH264_ENCODER_H

#include "encoder_backend.h"

/* Фабричный метод создания экземпляра бэкенда OpenH264 */
l4c_status_t l4c_openh264_encoder_create(l4c_encoder_backend_t **out_backend);

#endif /* L4C_OPENH264_ENCODER_H */
