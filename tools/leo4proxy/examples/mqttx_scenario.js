/**
 * MQTTX Script / Scenario Generator for Leo4 Extra Service
 * Generates dynamic event payloads and user properties for each publication in the 10-minute cycle.
 */

let eventCounter = 36823;

function handlePayload(index) {
  const devEventId = eventCounter++;
  const now = new Date();

  // Format Moscow time (+03:00) ISO string
  const pad = (n) => String(n).padStart(2, '0');
  const isoTime = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}T${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}+03:00`;
  const unixTs = Math.floor(now.getTime() / 1000);
  const corrId = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => {
    const r = (Math.random() * 16) | 0;
    const v = c === 'x' ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });

  return JSON.stringify({
    "101": devEventId,
    "102": isoTime,
    "200": 888,
    "300": [
      {
        "301": "044AFE42C76781",
        "302": 6,
        "303": 0
      }
    ]
  }, null, 2);
}

// Export for MQTTX Script engine
execute = handlePayload;
