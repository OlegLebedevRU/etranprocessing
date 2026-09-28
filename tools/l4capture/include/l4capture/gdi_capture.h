#ifndef L4C_GDI_CAPTURE_H
#define L4C_GDI_CAPTURE_H

#include "capture_backend.h"

/* Фабричный метод создания экземпляра GDI capture backend */
l4c_status_t l4c_gdi_capture_create(l4c_capture_backend_t **out_backend);

#endif /* L4C_GDI_CAPTURE_H */
