import { DurableObject } from "cloudflare:workers";

const hex = n => [...crypto.getRandomValues(new Uint8Array(n))].map(x=>x.toString(16).padStart(2,"0")).join("");
const digest = async text => [...new Uint8Array(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(text)))].map(x=>x.toString(16).padStart(2,"0")).join("");
const clean = (s,n) => typeof s==="string" && s.length>0 && [...s].length<=n && !/[\p{C}\t\r\n]/u.test(s) && s===s.trim();

// Ephemeral public lobby. Names are unverified nicknames, never accounts.
// No chat, SDP, IP or invitation credentials are written to storage or logs.
export class UpperLobby extends DurableObject {
  async fetch(request) {
    if(this.ctx.getWebSockets().length>=64)return new Response("Lobby full",{status:503});
    const [client,server]=Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({id:hex(8),name:"",version:request.headers.get("X-Lobby-Version")||"0.15.0",joined:Date.now(),seen:Date.now(),window:Date.now(),messages:0,chat:0,inviteAt:0});
    server.send("WELCOME\t1");const alarm=await this.ctx.storage.getAlarm();if(alarm===null||alarm>Date.now()+15000)await this.ctx.storage.setAlarm(Date.now()+15000);
    return new Response(null,{status:101,webSocket:client});
  }
  named(){return this.ctx.getWebSockets().filter(s=>s.readyState===1&&s.deserializeAttachment()?.name);}
  send(s,text){try{s.send(text);}catch{}}
  broadcast(text){for(const s of this.named())this.send(s,text);}
  roster(){this.broadcast("LIST\t"+this.named().map(s=>{const a=s.deserializeAttachment();return a.id+"\t"+a.name+"\t"+(a.peer?"Convidando":"Disponivel");}).join("\t"));}
  error(s,text){this.send(s,"ERROR\t"+text);}
  find(id){return this.named().find(s=>s.deserializeAttachment().id===id);}
  clear(s,text){
    const a=s.deserializeAttachment();if(!a?.peer)return;
    const other=this.find(a.peer),b=other?.deserializeAttachment();
    delete a.peer;delete a.until;delete a.host;s.serializeAttachment(a);
    if(b?.peer===a.id){delete b.peer;delete b.until;delete b.host;other.serializeAttachment(b);if(text)this.send(other,"NOTICE\t"+text);}
    if(text)this.send(s,"NOTICE\t"+text);
  }
  async webSocketMessage(s,message){
    if(typeof message!=="string" || new TextEncoder().encode(message).length>1024){s.close(4002,"Invalid message");return;}
    const a=s.deserializeAttachment(),now=Date.now();
    if(now-a.window>=1000){a.window=now;a.messages=0;}
    if(++a.messages>8){s.close(4008,"Rate limit");return;}
    a.seen=now;s.serializeAttachment(a);
    const p=message.split("\t");
    if(p[0]==="PING" && p.length===1){this.send(s,"PONG");return;}
    if(!a.name){
      if(p.length!==3 || p[0]!=="NAME" || p[1]!==a.version || !clean(p[2],20)){this.error(s,"Use a mesma versao do lobby e um apelido de 1 a 20 caracteres.");return;}
      if(this.named().some(x=>x.deserializeAttachment().name.toLocaleLowerCase()===p[2].toLocaleLowerCase())){this.error(s,"Apelido em uso. Escolha outro e reconecte.");return;}
      a.name=p[2];s.serializeAttachment(a);this.send(s,"YOU\t"+a.id);this.roster();return;
    }
    if(p[0]==="CHAT" && p.length===2 && clean(p[1],240)){
      if(now-a.chat<1200){this.error(s,"Aguarde um instante entre mensagens.");return;}
      a.chat=now;s.serializeAttachment(a);this.broadcast("CHAT\t"+a.name+"\t"+p[1]);return;
    }
    if(p[0]==="INVITE" && p.length===2){
      const other=this.find(p[1]),b=other?.deserializeAttachment();
      if(!other || other===s || a.peer || b.peer || now-a.inviteAt<5000){this.error(s,"Jogador indisponivel ou convite ja pendente.");return;}
      a.peer=b.id;a.host=true;a.until=now+30000;a.inviteAt=now;b.peer=a.id;b.host=false;b.until=a.until;
      s.serializeAttachment(a);other.serializeAttachment(b);
      this.send(other,"INVITE\t"+a.id+"\t"+a.name);this.send(s,"NOTICE\tConvite enviado. Aguarde a resposta (30 segundos).");this.roster();return;
    }
    if((p[0]==="ACCEPT" || p[0]==="DECLINE") && p.length===2){
      const other=this.find(p[1]),b=other?.deserializeAttachment();
      if(!other || a.peer!==b.id || b.peer!==a.id || a.host || !b.host || a.until<now){this.error(s,"O convite expirou.");return;}
      if(p[0]==="DECLINE"){this.clear(s,"Convite recusado.");this.roster();return;}
      // Reserve both endpoints before awaiting room creation; a duplicate accept
      // cannot start another game or reveal either player's private token.
      a.host=true;s.serializeAttachment(a);
      const room=hex(16),hostToken=hex(32),guestToken=hex(32);
      const result=await this.env.ROOMS.get(this.env.ROOMS.idFromName(room)).fetch(new Request("https://room.internal/init",{method:"POST",body:JSON.stringify({host:await digest(hostToken),guest:await digest(guestToken),p2p:true,expires:now+120000})}));
      if(!result.ok){this.clear(s,"Nao foi possivel criar a partida. Tente novamente.");this.roster();return;}
      if(!this.ctx.getWebSockets().includes(s)||!this.ctx.getWebSockets().includes(other)||s.deserializeAttachment().peer!==b.id||other.deserializeAttachment().peer!==a.id){this.clear(s,"Jogador desconectado.");this.roster();return;}
      this.send(other,"START\t0\t"+room+"\t"+hostToken);this.send(s,"START\t1\t"+room+"\t"+guestToken);
      this.clear(s);s.close(1000,"Starting match");other.close(1000,"Starting match");this.roster();return;
    }
    this.error(s,"Comando invalido.");
  }
  async webSocketClose(s){this.clear(s,"O outro jogador saiu do lobby.");try{s.close(1000,"Closed");}catch{}this.roster();}
  async webSocketError(s){await this.webSocketClose(s);}
  async alarm(){
    const now=Date.now();
    for(const s of this.ctx.getWebSockets()){
      const a=s.deserializeAttachment();
      if(a.until<now)this.clear(s,"Convite expirado.");
      if(now-a.seen>90000 || (!a.name&&now-a.joined>15000)){this.clear(s,"Jogador desconectado.");s.close(4001,"Idle");}
    }
    this.roster();if(this.ctx.getWebSockets().length)await this.ctx.storage.setAlarm(now+15000);
  }
}
