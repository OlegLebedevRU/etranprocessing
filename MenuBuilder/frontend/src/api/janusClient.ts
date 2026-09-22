export interface JanusClientOptions {
  wsUrl: string;
  mountpointId: number;
  pin?: string;
  onRemoteTrack: (stream: MediaStream) => void;
  onStatusChange?: (status: string) => void;
  onError?: (error: Error | string) => void;
}

interface PendingTx {
  resolve: (value: any) => void;
  reject: (reason: any) => void;
  timer: any;
}

export class JanusStreamingClient {
  private wsUrl: string;
  private mountpointId: number;
  private pin?: string;
  private onRemoteTrack: (stream: MediaStream) => void;
  private onStatusChange?: (status: string) => void;
  private onError?: (error: Error | string) => void;

  private ws: WebSocket | null = null;
  private pc: RTCPeerConnection | null = null;
  private sessionId: number | null = null;
  private handleId: number | null = null;
  private keepaliveTimer: any = null;
  private pendingTransactions = new Map<string, PendingTx>();
  private isDestroyed = false;
  private isReconnecting = false;
  private reconnectAttempts = 0;
  private readonly maxReconnectAttempts = 5;
  private readonly baseReconnectDelayMs = 1000;

  constructor(options: JanusClientOptions) {
    this.wsUrl = options.wsUrl;
    this.mountpointId = options.mountpointId;
    this.pin = options.pin;
    this.onRemoteTrack = options.onRemoteTrack;
    this.onStatusChange = options.onStatusChange;
    this.onError = options.onError;
  }

  private randomTx(): string {
    return Math.random().toString(36).substring(2, 12);
  }

  private send(msg: any): void {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(msg));
    }
  }

  private sendTransaction(msg: any, timeoutMs = 10000): Promise<any> {
    const tx = this.randomTx();
    msg.transaction = tx;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.pendingTransactions.has(tx)) {
          this.pendingTransactions.delete(tx);
          reject(new Error(`Janus transaction timeout for ${msg.janus || "msg"}`));
        }
      }, timeoutMs);

      this.pendingTransactions.set(tx, { resolve, reject, timer });
      this.send(msg);
    });
  }

  private async handleJsepOffer(jsep: RTCSessionDescriptionInit): Promise<void> {
    if (!this.pc || this.isDestroyed || !this.sessionId || !this.handleId) return;

    try {
      await this.pc.setRemoteDescription(new RTCSessionDescription(jsep));
      const answer = await this.pc.createAnswer();
      await this.pc.setLocalDescription(answer);

      this.send({
        janus: "message",
        session_id: this.sessionId,
        handle_id: this.handleId,
        transaction: this.randomTx(),
        body: { request: "start" },
        jsep: {
          type: answer.type,
          sdp: answer.sdp,
        },
      });
      this.onStatusChange?.("streaming");
    } catch (err: any) {
      this.onError?.(err?.message || "Ошибка обработки SDP offer от Janus");
    }
  }

  private handleMessage(data: any): void {
    if (this.isDestroyed) return;

    const janusType = data?.janus;

    // Direct transaction responses
    const tx = data?.transaction;
    if (tx && this.pendingTransactions.has(tx)) {
      if (janusType === "ack") {
        // Ack received, await subsequent event/success for this transaction
        return;
      }
      const pending = this.pendingTransactions.get(tx)!;
      clearTimeout(pending.timer);
      this.pendingTransactions.delete(tx);

      if (janusType === "error") {
        const errDetail = data?.error?.reason || "Janus error";
        pending.reject(new Error(errDetail));
        return;
      }

      if (data?.plugindata?.data?.error) {
        const errDetail = data.plugindata.data.error;
        pending.reject(new Error(errDetail));
        return;
      }

      pending.resolve(data);
    }

    // JSEP offer on streaming plugin
    if (data?.jsep && data.jsep.type === "offer") {
      this.handleJsepOffer(data.jsep);
      return;
    }

    if (janusType === "webrtcup") {
      this.onStatusChange?.("webrtcup");
    } else if (janusType === "media") {
      this.onStatusChange?.("media");
    } else if (janusType === "hangup") {
      this.onStatusChange?.("hangup");
    } else if (janusType === "error") {
      const reason = data?.error?.reason || "Janus event error";
      if (
        reason.includes("Couldn't find any handle") ||
        reason.includes("Couldn't find any session")
      ) {
        this.handleConnectionLoss();
      } else {
        this.onError?.(reason);
      }
    }
  }

  private cleanup(): void {
    if (this.keepaliveTimer) {
      clearInterval(this.keepaliveTimer);
      this.keepaliveTimer = null;
    }
    for (const [, pending] of this.pendingTransactions) {
      clearTimeout(pending.timer);
      pending.reject(new Error("Reconnecting"));
    }
    this.pendingTransactions.clear();
    if (this.pc) {
      this.pc.close();
      this.pc = null;
    }
    if (this.ws) {
      try {
        this.ws.close();
      } catch {
        /* ignore */
      }
      this.ws = null;
    }
    this.sessionId = null;
    this.handleId = null;
  }

  private async handleConnectionLoss(): Promise<void> {
    if (this.isDestroyed || this.isReconnecting) return;
    this.isReconnecting = true;

    while (!this.isDestroyed && this.reconnectAttempts < this.maxReconnectAttempts) {
      this.cleanup();
      this.onStatusChange?.("reconnecting");

      const delay = this.baseReconnectDelayMs * Math.pow(2, this.reconnectAttempts);
      await new Promise((resolve) => setTimeout(resolve, delay));
      if (this.isDestroyed) break;

      this.reconnectAttempts++;
      try {
        await this.start();
        this.reconnectAttempts = 0;
        this.onStatusChange?.("reconnected");
        break;
      } catch {
        /* retry */
      }
    }

    if (this.reconnectAttempts >= this.maxReconnectAttempts) {
      this.onError?.("Не удалось восстановить подключение к видеосервису");
    }
    this.isReconnecting = false;
  }

  public async start(): Promise<void> {
    this.isDestroyed = false;
    this.onStatusChange?.("connecting");

    await new Promise<void>((resolve, reject) => {
      try {
        this.ws = new WebSocket(this.wsUrl, "janus-protocol");
      } catch (err) {
        reject(err);
        return;
      }

      this.ws.onopen = () => resolve();
      this.ws.onerror = (e) => reject(new Error("Ошибка WebSocket соединения с Janus"));
      this.ws.onclose = () => {
        if (!this.isDestroyed) {
          this.handleConnectionLoss();
        }
      };
      this.ws.onmessage = (event) => {
        try {
          const parsed = JSON.parse(event.data);
          this.handleMessage(parsed);
        } catch (err) {
          console.error("Janus JSON parse error:", err);
        }
      };
    });

    // 1. Create Session
    const createResp = await this.sendTransaction({ janus: "create" });
    this.sessionId = createResp?.data?.id;
    if (!this.sessionId) {
      throw new Error("Не удалось получить session_id от Janus");
    }

    // Start keepalive every 25s
    this.keepaliveTimer = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN && this.sessionId) {
        this.send({
          janus: "keepalive",
          session_id: this.sessionId,
          transaction: this.randomTx(),
        });
      }
    }, 25000);

    // 2. Attach Streaming Plugin
    const attachResp = await this.sendTransaction({
      janus: "attach",
      session_id: this.sessionId,
      plugin: "janus.plugin.streaming",
    });
    this.handleId = attachResp?.data?.id;
    if (!this.handleId) {
      throw new Error("Не удалось подключить плагин streaming в Janus");
    }

    // 3. Setup RTCPeerConnection
    this.pc = new RTCPeerConnection({
      iceServers: [{ urls: "stun:stun.l.google.com:19302" }],
    });

    this.pc.onicecandidate = (event) => {
      if (!this.sessionId || !this.handleId || this.isDestroyed) return;
      if (event.candidate) {
        this.send({
          janus: "trickle",
          session_id: this.sessionId,
          handle_id: this.handleId,
          transaction: this.randomTx(),
          candidate: event.candidate.toJSON ? event.candidate.toJSON() : event.candidate,
        });
      } else {
        this.send({
          janus: "trickle",
          session_id: this.sessionId,
          handle_id: this.handleId,
          transaction: this.randomTx(),
          candidate: { completed: true },
        });
      }
    };

    this.pc.ontrack = (event) => {
      if (event.streams && event.streams[0]) {
        this.onRemoteTrack(event.streams[0]);
      } else {
        this.onRemoteTrack(new MediaStream([event.track]));
      }
    };

    // 4. Request "watch" mountpoint
    const watchBody: Record<string, any> = {
      request: "watch",
      id: this.mountpointId,
    };
    if (this.pin) {
      watchBody.pin = this.pin;
    }
    await this.sendTransaction({
      janus: "message",
      session_id: this.sessionId,
      handle_id: this.handleId,
      body: watchBody,
    });
  }

  public async stop(): Promise<void> {
    if (this.isDestroyed) return;
    this.isDestroyed = true;

    if (this.keepaliveTimer) {
      clearInterval(this.keepaliveTimer);
      this.keepaliveTimer = null;
    }

    if (this.ws && this.ws.readyState === WebSocket.OPEN && this.sessionId && this.handleId) {
      try {
        this.send({
          janus: "message",
          session_id: this.sessionId,
          handle_id: this.handleId,
          transaction: this.randomTx(),
          body: { request: "stop" },
        });
        this.send({
          janus: "detach",
          session_id: this.sessionId,
          handle_id: this.handleId,
          transaction: this.randomTx(),
        });
      } catch {
        /* ignore */
      }
    }

    for (const [, pending] of this.pendingTransactions) {
      clearTimeout(pending.timer);
      pending.reject(new Error("Janus client stopped"));
    }
    this.pendingTransactions.clear();

    if (this.pc) {
      this.pc.close();
      this.pc = null;
    }

    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }

    this.sessionId = null;
    this.handleId = null;
    this.onStatusChange?.("stopped");
  }
}
