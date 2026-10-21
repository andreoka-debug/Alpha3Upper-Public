import { DurableObject } from "cloudflare:workers";
export { UpperLobby } from "./lobby.js";

const hex = (n) => [...crypto.getRandomValues(new Uint8Array(n))].map(x => x.toString(16).padStart(2, "0")).join("");
const digest = async text => [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)))].map(x => x.toString(16).padStart(2,"0")).join("");
const json = (value, status = 200) => new Response(JSON.stringify(value), {status, headers: {"Content-Type":"application/json", "Cache-Control":"no-store"}});

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/health" && request.method === "GET") return json({service:"alpha3-upper-netplay", protocol:1});
    if((url.pathname==="/lobby"||url.pathname==="/lobby/public-v1") && request.method==="GET"){
      if(request.headers.get("Upgrade")?.toLowerCase()!=="websocket")return json({error:"WebSocket required"},426);
      if(request.headers.get("X-Upper3-Client")!=="1" || request.headers.has("Origin"))return json({error:"Native client required"},403);
      const {success}=await env.LOBBY_LIMITER.limit({key:request.headers.get("CF-Connecting-IP")||"local"});
      if(!success)return json({error:"Try later"},429);
      const publicLobby=url.pathname==="/lobby/public-v1",headers=new Headers(request.headers);headers.set("X-Lobby-Version",publicLobby?"0.16.0":"0.15.0");
      return env.LOBBY.get(env.LOBBY.idFromName(publicLobby?"global-public-v1":"global-v1")).fetch(new Request(request,{headers}));
    }
    if ((url.pathname === "/rooms" || url.pathname === "/p2p/rooms") && request.method === "POST") {
      // Native application only. A custom header prevents browser form/CSRF
      // creation; it is not an authentication secret. Invites are capabilities.
      if (request.headers.get("X-Upper3-Client") !== "1" || request.headers.has("Origin")) return json({error:"Native client required"},403);
      if (!env.CREATE_LIMITER) return json({error:"Creation limit not configured"},503);
      const {success} = await env.CREATE_LIMITER.limit({key:request.headers.get("CF-Connecting-IP") || "local"});
      if (!success) return json({error:"Try later"},429);
      const room = hex(16), hostToken = hex(32), guestToken = hex(32);
      const stub = env.ROOMS.get(env.ROOMS.idFromName(room));
      const result = await stub.fetch(new Request("https://room.internal/init", {method:"POST",body:JSON.stringify({host:await digest(hostToken),guest:await digest(guestToken),p2p:url.pathname.startsWith("/p2p/"),expires:Date.now()+15*60*1000})}));
      if (!result.ok) return json({error:"Room unavailable"},503);
      return json({room,hostToken,guestToken},201);
    }
    const match = /^(\/p2p)?\/rooms\/([a-f0-9]{32})\/(host|guest)$/.exec(url.pathname);
    if (match && request.method === "GET") {
      if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return json({error:"WebSocket required"},426);
      if (request.headers.has("Origin")) return json({error:"Native client required"},403);
      return env.ROOMS.get(env.ROOMS.idFromName(match[2])).fetch(request);
    }
    return json({error:"Not found"},404);
  }
};

export class UpperRoom extends DurableObject {
  async fetch(request) {
    const url = new URL(request.url);
    if (url.hostname === "room.internal" && url.pathname === "/init" && request.method === "POST") {
      if (await this.ctx.storage.get("meta")) return json({error:"Exists"},409);
      const meta = await request.json();
      if (!/^[a-f0-9]{64}$/.test(meta.host) || !/^[a-f0-9]{64}$/.test(meta.guest)) return json({error:"Invalid"},400);
      await this.ctx.storage.put("meta", {...meta, started:false});
      await this.ctx.storage.setAlarm(Date.now()+5000);return json({ok:true});
    }
    const meta = await this.ctx.storage.get("meta");this.meta=meta;
    if (!meta || meta.expires < Date.now()) return json({error:"Expired"},410);
    if (!!meta.p2p !== url.pathname.startsWith("/p2p/")) return json({error:"Wrong room mode"},409);
    const role = url.pathname.endsWith("/host") ? "host" : "guest";
    const token = /^Bearer ([a-f0-9]{64})$/.exec(request.headers.get("Authorization") || "")?.[1];
    if (!token || await digest(token) !== meta[role]) return json({error:"Unauthorized"},401);
    const sockets = this.ctx.getWebSockets();
    if (meta.started || sockets.length >= 2 || sockets.some(s => s.deserializeAttachment()?.role === role)) return json({error:"Room occupied"},409);
    const pair = new WebSocketPair(), [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);server.serializeAttachment({role,window:Date.now(),messages:0,lastFrame:-1,hello:false});
    const peers = this.ctx.getWebSockets();
    if (peers.length === 2) {
      meta.started = true;meta.expires = Date.now()+(meta.p2p?2:60)*60*1000;await this.ctx.storage.put("meta",meta);
      for (const peer of peers) peer.send("READY");
      await this.ctx.storage.setAlarm(meta.expires);
    } else server.send("WAIT");
    return new Response(null,{status:101,webSocket:client});
  }
  async webSocketMessage(socket, message) {
    const meta = this.meta || await this.ctx.storage.get("meta");this.meta=meta;
    if (meta?.p2p) {
      // Signaling only: exactly one full ICE/DTLS description per peer.
      // No game commands, binary messages, trickle streams or stored SDP.
      const a=socket.deserializeAttachment(),prefix=a.role==="host"?"O\n":"A\n";
      if(!meta.started || meta.expires<Date.now() || typeof message!=="string" || message.length>16384 || new TextEncoder().encode(message).length>16384 || !message.startsWith(prefix+"v=0\r\n") || a.description) return this.end(4002,"Invalid signaling");
      if(!message.includes("a=fingerprint:sha-256 ") || !message.includes("a=ice-ufrag:") || !message.includes("m=application "))return this.end(4002,"Invalid description");
      const other=this.ctx.getWebSockets().find(s=>s!==socket);
      if(!other)return this.end(4001,"Peer disconnected");
      a.description=true;socket.serializeAttachment(a);other.send(message);return;
    }
    if (!meta?.started || meta.expires<Date.now() || !(message instanceof ArrayBuffer) || message.byteLength !== 128) return this.end(4002,"Invalid message");
    const v = new DataView(message), type = v.getUint32(8);
    if (v.getUint32(0)!==0x55334e50 || v.getUint32(4)!==1 || v.getUint32(12)!==128 || type<1 || type>5) return this.end(4002,"Invalid protocol");
    const a = socket.deserializeAttachment(),now=Date.now();
    if (now-a.window>=1000) {a.window=now;a.messages=0;}
    if (++a.messages>240) return this.end(4008,"Rate exceeded");
    if (type===1) {if(a.hello || v.getUint32(16)!==(a.role==="host"?0:1))return this.end(4002,"Invalid handshake");a.hello=true;}
    else if (!a.hello) return this.end(4002,"Handshake required");
    if(type===2) {const frame=v.getUint32(16);if(frame<=a.lastFrame || (v.getUint32(20)&~0xfaf9)!==0)return this.end(4002,"Invalid inputs");a.lastFrame=frame;}
    socket.serializeAttachment(a);
    const other = this.ctx.getWebSockets().find(s=>s!==socket);
    if (!other) return this.end(4001,"Peer disconnected");
    other.send(message);
  }
  async webSocketClose() { await this.end(4001,"Peer disconnected"); }
  async webSocketError() { await this.end(4001,"Connection error"); }
  async end(code, reason) {
    for (const s of this.ctx.getWebSockets()) {try{s.close(code,reason);}catch{}}
    await this.ctx.storage.deleteAll();
    await this.ctx.storage.deleteAlarm();this.meta=null;
  }
  async alarm() {
    const meta=await this.ctx.storage.get("meta");
    if(!meta || meta.expires<Date.now())return this.end(4000,"Expired");
    if(!meta.started){for(const s of this.ctx.getWebSockets())s.send("WAIT");await this.ctx.storage.setAlarm(Math.min(meta.expires,Date.now()+5000));}
    else await this.ctx.storage.setAlarm(meta.expires);
  }
}
