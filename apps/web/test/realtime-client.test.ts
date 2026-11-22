import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { RealtimeClient, type Scheduler, type SocketLike } from "@/lib/realtime-client";
import { firmwareFixturesDir } from "./paths";

class FakeScheduler implements Scheduler {
  time = 0;
  private seq = 0;
  private tasks = new Map<number, { at: number; fn: () => void; every?: number }>();
  setTimeout(fn: () => void, ms: number) {
    const id = ++this.seq;
    this.tasks.set(id, { at: this.time + ms, fn });
    return id;
  }
  clearTimeout(handle: unknown) {
    this.tasks.delete(handle as number);
  }
  setInterval(fn: () => void, ms: number) {
    const id = ++this.seq;
    this.tasks.set(id, { at: this.time + ms, fn, every: ms });
    return id;
  }
  clearInterval(handle: unknown) {
    this.tasks.delete(handle as number);
  }
  now() {
    return this.time;
  }
  advance(ms: number) {
    const end = this.time + ms;
    for (;;) {
      const next = [...this.tasks.entries()].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
      if (!next) break;
      const [id, task] = next;
      this.time = task.at;
      if (task.every) task.at += task.every;
      else this.tasks.delete(id);
      task.fn();
    }
    this.time = end;
  }
}

class FakeSocket implements SocketLike {
  readyState = 0;
  protocol = "";
  sent: string[] = [];
  closed: { code?: number } | null = null;
  onopen: SocketLike["onopen"] = null;
  onclose: SocketLike["onclose"] = null;
  onerror: SocketLike["onerror"] = null;
  onmessage: SocketLike["onmessage"] = null;
  constructor(readonly url: string, readonly protocols: string[]) {}
  send(data: string) {
    this.sent.push(data);
  }
  close(code?: number) {
    this.closed = { code };
    this.readyState = 3;
  }
  open(protocol = "sentinel.v1") {
    this.readyState = 1;
    this.protocol = protocol;
    this.onopen?.({});
  }
  receive(data: unknown) {
    this.onmessage?.({ data });
  }
  drop(code = 1006) {
    this.readyState = 3;
    this.onclose?.({ code, reason: "", wasClean: false });
  }
}

function setup() {
  const scheduler = new FakeScheduler();
  const sockets: FakeSocket[] = [];
  const messages: string[] = [];
  const states: string[] = [];
  const client = new RealtimeClient({
    url: "ws://192.168.4.1:81/realtime",
    scheduler,
    random: () => 1,
    createSocket: (url, protocols) => {
      const socket = new FakeSocket(url, protocols);
      sockets.push(socket);
      return socket;
    },
    onMessage: message => messages.push(message.type),
    onState: state => states.push(state),
  });
  return { scheduler, sockets, messages, states, client };
}

const fixture = (name: string) => readFileSync(join(firmwareFixturesDir, name), "utf8").trim();

describe("RealtimeClient", () => {
  it("negotiates sentinel.v1, sends hello then periodic heartbeats", () => {
    const { client, sockets, scheduler } = setup();
    client.start();
    const socket = sockets[0]!;
    expect(socket.protocols).toEqual(["sentinel.v1"]);
    socket.open();
    const hello = JSON.parse(socket.sent[0]!) as { type: string; sequence: number; source: string; payload: { role: string } };
    expect(hello).toMatchObject({ type: "connection.hello", sequence: 1, source: "web-dashboard", payload: { role: "web" } });
    scheduler.advance(15000);
    scheduler.advance(15000);
    const types = socket.sent.map(s => (JSON.parse(s) as { type: string; sequence: number }));
    expect(types.map(t => t.type)).toEqual(["connection.hello", "connection.heartbeat", "connection.heartbeat"]);
    expect(types.map(t => t.sequence)).toEqual([1, 2, 3]);
  });

  it("refuses a server that did not select the subprotocol", () => {
    const { client, sockets } = setup();
    client.start();
    sockets[0]!.open("");
    expect(sockets[0]!.closed?.code).toBe(1002);
    expect(sockets[0]!.sent).toHaveLength(0);
  });

  it("delivers valid frames and counts invalid, unknown and binary ones", () => {
    const { client, sockets, messages } = setup();
    client.start();
    const socket = sockets[0]!;
    socket.open();
    socket.receive(fixture("firmware-rf.json"));
    socket.receive(fixture("firmware-event.json"));
    socket.receive("{bad");
    socket.receive(fixture("firmware-heartbeat.json").replace('"connection.heartbeat"', '"future.type"'));
    socket.receive(new ArrayBuffer(4));
    expect(messages).toEqual(["rf.telemetry", "event"]);
    expect(client.stats).toMatchObject({ framesReceived: 2, framesInvalid: 1, framesUnknownType: 1, framesBinary: 1 });
  });

  it("reconnects with exponential backoff and resets it after a successful open", () => {
    const { client, sockets, scheduler } = setup();
    client.start();
    sockets[0]!.drop();
    expect(client.stats.nextRetryMs).toBe(1000);
    scheduler.advance(999);
    expect(sockets).toHaveLength(1);
    scheduler.advance(1);
    expect(sockets).toHaveLength(2);
    sockets[1]!.drop();
    expect(client.stats.nextRetryMs).toBe(2000);
    scheduler.advance(2000);
    sockets[2]!.open();
    sockets[2]!.drop();
    expect(client.stats.nextRetryMs).toBe(1000);
    expect(client.stats.reconnects).toBe(3);
  });

  it("closes a silent link only when the firmware is expected to stream", () => {
    const { client, sockets, scheduler } = setup();
    client.start();
    sockets[0]!.open();
    scheduler.advance(60000);
    expect(sockets[0]!.closed).toBeNull();
    client.setExpectTraffic(true);
    scheduler.advance(19999);
    sockets[0]!.receive(fixture("firmware-heartbeat.json"));
    scheduler.advance(19999);
    expect(sockets[0]!.closed).toBeNull();
    scheduler.advance(1);
    expect(sockets[0]!.closed?.code).toBe(4000);
    expect(client.stats.watchdogCloses).toBe(1);
    expect(client.currentState).toBe("closed");
  });

  it("sends goodbye and stops reconnecting on stop()", () => {
    const { client, sockets, scheduler } = setup();
    client.start();
    sockets[0]!.open();
    client.stop();
    expect(JSON.parse(sockets[0]!.sent.at(-1)!)).toMatchObject({ type: "connection.goodbye", payload: { reason: "client-request" } });
    scheduler.advance(120000);
    expect(sockets).toHaveLength(1);
    expect(client.currentState).toBe("idle");
  });
});
