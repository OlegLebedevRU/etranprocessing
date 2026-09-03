
---

# mTLS для MQTT с non-exportable клиентским сертификатом из Windows Certificate Store

## Проблема

Ни одна из популярных MQTT-библиотек (Paho C, Paho Python, Mosquitto) не поддерживает использование клиентского сертификата с non-exportable приватным ключом из Windows Certificate Store. Все они используют OpenSSL, который требует приватный ключ в виде PEM-файла.

Единственный способ использовать non-exportable ключ для TLS — через **Windows SChannel** (нативный TLS-стек Windows), который работает с ключами через handle, не извлекая их из хранилища.

## Выбранный вариант: Python SChannel TLS proxy + paho-mqtt

### Архитектура

```
paho-mqtt  ──plain TCP──►  SChannel TLS Proxy  ──TLS (mTLS)──►  MQTT Broker
(localhost:18883)            (Python, pywin32)                     (remote:8883)
```

### Структура файлов

```
schannel_mqtt/
├── schannel_proxy.py      # TLS-прокси на SChannel (~400 строк)
├── cert_store.py          # Поиск сертификата в Windows Store (~80 строк)
├── mqtt_client_example.py # Пример MQTT-клиента через прокси
└── requirements.txt       # pywin32, paho-mqtt
```

### Шаг 1: `cert_store.py` — поиск сертификата

- `ctypes` вызовы: `CertOpenSystemStore`, `CertFindCertificateInStore`
- Поиск по subject name или thumbprint (SHA-1 hash) в store "MY"
- Возврат `PCCERT_CONTEXT` для передачи в SChannel

### Шаг 2: `schannel_proxy.py` — TLS прокси

Этапы SChannel handshake:
1. `AcquireCredentialsHandle` — указать сертификат из `cert_store.py`
2. Цикл `InitializeSecurityContext` ↔ отправка/получение handshake data
3. После handshake: `EncryptMessage`/`DecryptMessage` для пересылки данных

Архитектура прокси:
- `asyncio` TCP server на `localhost:18883`
- При подключении клиента (paho-mqtt) → установить TLS к брокеру
- Проксировать данные: client→proxy→`EncryptMessage`→broker и обратно
- Поддержка нескольких одновременных подключений

### Шаг 3: `mqtt_client_example.py` — клиент

```python
import paho.mqtt.client as mqtt

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.connect("127.0.0.1", 18883)  # к локальному прокси
client.subscribe("test/#")
client.loop_forever()
```

### Шаг 4: Зависимости

```
# requirements.txt
pywin32>=306
paho-mqtt>=2.0.0
```

### Ключевые Windows API

```
# cert_store.py (ctypes)
Crypt32.dll:
  CertOpenSystemStoreW(0, "MY")
  CertFindCertificateInStore(hStore, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR, subject, NULL)
  CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, ...)
  CertFreeCertificateContext(cert)
  CertCloseStore(hStore, 0)

# schannel_proxy.py (pywin32 win32security)
Secur32.dll:
  AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_OUTBOUND, NULL, &schannel_cred, ...)
  InitializeSecurityContext(&hcred, NULL, target_name, flags, 0, ..., &ctx, &output_desc, &attrs, &expiry)
  EncryptMessage(&ctx, 0, &desc, 0)
  DecryptMessage(&ctx, &desc, 0, &qop)
  DeleteSecurityContext(&ctx)
  FreeCredentialsHandle(&hcred)
```

### Верификация

1. Установить тестовый сертификат с non-exportable ключом в Windows Store "MY"
2. Запустить прокси: `python schannel_proxy.py --broker mqtt-broker:8883 --cert-subject "MyClientCert"`
3. Запустить MQTT-клиент: `python mqtt_client_example.py`
4. Выполнить pub/sub, убедиться в mTLS handshake
5. Проверить логи SChannel (TLS version, cipher suite, client cert subject)

---
