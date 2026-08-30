import contextlib
import json
import logging
import threading
from typing import Any

from app.config import settings

logger = logging.getLogger(__name__)


class GaugeStore:
    """In-memory thread-safe storage of active device gauge snapshots."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._by_device_id: dict[int, dict[str, Any]] = {}
        self._by_sn: dict[str, dict[str, Any]] = {}

    def get_by_device_id(self, device_id: int) -> dict[str, Any] | None:
        with self._lock:
            return self._by_device_id.get(device_id)

    def get_by_sn(self, sn: str) -> dict[str, Any] | None:
        with self._lock:
            return self._by_sn.get(sn)

    def set_snapshot(self, snapshot: dict[str, Any]) -> None:
        dev_id = snapshot.get("device_id")
        sn = snapshot.get("sn")
        with self._lock:
            if dev_id is not None:
                self._by_device_id[int(dev_id)] = snapshot
            if sn:
                self._by_sn[str(sn)] = snapshot

    def get_all(self) -> dict[int, dict[str, Any]]:
        with self._lock:
            return dict(self._by_device_id)

    def update_enrichment(self, device_id: int, updates: dict[str, Any]) -> None:
        with self._lock:
            snapshot = self._by_device_id.get(device_id)
            if snapshot:
                enrichment = snapshot.setdefault("internal_enrichment", {})
                enrichment.update(updates)

    def clear(self) -> None:
        with self._lock:
            self._by_device_id.clear()
            self._by_sn.clear()


gauge_store = GaugeStore()


class GaugeMqttBus:
    """MQTT client handling Retain snapshots on `dev/{SN}/gauge/state`."""

    def __init__(self) -> None:
        self._client: Any = None
        self._started = False
        self._lock = threading.Lock()

    def start(self) -> None:
        if not settings.mqtt_enabled:
            logger.info("MQTT Gauges bus is disabled in settings")
            return

        with self._lock:
            if self._started:
                return

            try:
                import paho.mqtt.client as mqtt
                from paho.mqtt.enums import CallbackAPIVersion

                client = mqtt.Client(
                    callback_api_version=CallbackAPIVersion.VERSION2,
                    client_id=f"menubuilder_{id(self)}",
                    protocol=mqtt.MQTTv5,
                )
                if settings.mqtt_username:
                    client.username_pw_set(
                        settings.mqtt_username, settings.mqtt_password
                    )

                def on_connect(client, userdata, flags, reason_code, properties):
                    if reason_code.is_failure:
                        logger.warning(
                            "Failed to connect to RabbitMQ MQTT: %s", reason_code
                        )
                        return
                    logger.info(
                        "Connected to RabbitMQ MQTT broker at %s:%d",
                        settings.mqtt_host,
                        settings.mqtt_port,
                    )
                    prefix = settings.mqtt_topic_prefix.strip("/")
                    topic = f"{prefix}/+/gauge/state"
                    client.subscribe(topic, qos=1)
                    logger.info("Subscribed to MQTT retained topic: %s", topic)

                def on_message(client, userdata, msg):
                    try:
                        payload = json.loads(msg.payload.decode("utf-8"))
                        gauge_store.set_snapshot(payload)
                        logger.debug(
                            "Received Gauge snapshot for SN: %s", payload.get("sn")
                        )
                    except Exception as exc:  # noqa: BLE001
                        logger.debug(
                            "Failed to parse Gauge MQTT payload on %s: %s",
                            msg.topic,
                            exc,
                        )

                client.on_connect = on_connect
                client.on_message = on_message

                # Non-blocking connection in background loop
                client.connect_async(
                    settings.mqtt_host, settings.mqtt_port, keepalive=60
                )
                client.loop_start()

                self._client = client
                self._started = True
            except Exception as e:  # noqa: BLE001
                logger.warning("Could not initialize MQTT Gauges client: %s", e)

    def publish_snapshot(self, sn: str, snapshot: dict[str, Any]) -> None:
        """Publish snapshot to retain topic `dev/{sn}/gauge/state` and update local store."""
        gauge_store.set_snapshot(snapshot)

        if not self._started or not self._client:
            return

        try:
            prefix = settings.mqtt_topic_prefix.strip("/")
            topic = f"{prefix}/{sn}/gauge/state"
            payload = json.dumps(snapshot, ensure_ascii=False)
            self._client.publish(topic, payload=payload, qos=1, retain=True)
            logger.debug("Published retained Gauge state to %s", topic)
        except Exception as exc:  # noqa: BLE001
            logger.warning("Failed to publish Gauge state to MQTT for %s: %s", sn, exc)

    def stop(self) -> None:
        with self._lock:
            if self._started and self._client:
                with contextlib.suppress(Exception):
                    self._client.loop_stop()
                    self._client.disconnect()
                self._started = False
                self._client = None


gauge_mqtt_bus = GaugeMqttBus()
