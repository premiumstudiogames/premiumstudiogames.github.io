/* Voice calls, voice parties and Remote Play Together, peer to peer (WebRTC). Firestore only carries the handshake.
   calls/<id> = {host, members[], names{}, kind:'voice'|'party'|'remote', game, in[], status, at}
   calls/<id>/sig/<auto> = {from, to, type:'offer'|'answer'|'ice', data, at} */
export function makeRTC(H){
  const {fs, F, me, toast, esc, PS} = H;   // F = firestore functions
  const ICE = {iceServers:[{urls:["stun:stun.l.google.com:19302", "stun:stun1.l.google.com:19302"]}]};
  let cur = null;        // {id, data, peers:{uid:{pc, dc}}, local, unsubs:[], muted, remoteVideo}
  const incoming = {};   // ringing calls for me
  let ringUnsub = null, onChange = () => {};
  function watchIncoming(){
    if (ringUnsub) ringUnsub();
    const u = me(); if (!u) return;
    ringUnsub = F.onSnapshot(F.query(F.collection(fs, "calls"), F.where("members", "array-contains", u.uid)), s => {
      for (const k of Object.keys(incoming)) delete incoming[k];
      s.forEach(d => { const c = d.data(); if (c.status !== "ended" && c.host !== u.uid && !(c.in || []).includes(u.uid) && Date.now() - ((c.at && c.at.toMillis && c.at.toMillis()) || Date.now()) < 10 * 60000) incoming[d.id] = c; });
      onChange();
    }, () => {});
  }
  async function mic(){ try { return await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true, noiseSuppression:true}, video:false}); } catch(e) { toast("ما قدرت أستخدم المايك: اسمح له من إعدادات الويندوز"); throw e; } }
  async function start(kind, members, names, extra){
    const u = me(); if (!u) return; if (cur) await leave();
    const ref = F.doc(F.collection(fs, "calls"));
    await F.setDoc(ref, {host:u.uid, members:[u.uid, ...members], names:{...names, [u.uid]:H.myName()}, kind, game:(extra && extra.game) || "", in:[u.uid], status:"live", at:F.serverTimestamp()});
    await join(ref.id, true, extra);
  }
  async function join(id, isHost, extra){
    const u = me(); if (!u) return; if (cur && cur.id !== id) await leave();
    const ref = F.doc(fs, "calls", id), snap = await F.getDoc(ref); if (!snap.exists()) return toast("المكالمة انتهت");
    const data = snap.data();
    let local = null;
    if (data.kind === "remote") { if (isHost) { local = await H.gameStream(extra && extra.key); if (!local) { await F.updateDoc(ref, {status:"ended"}); return; } } }
    else local = await mic();
    cur = {id, data, peers:{}, local, unsubs:[], muted:false, isHost, key:extra && extra.key};
    // everyone already inside gets an offer from me; the ones who come later send me theirs
    cur.unsubs.push(F.onSnapshot(F.query(F.collection(fs, "calls", id, "sig"), F.where("to", "==", u.uid)), s => {
      s.docChanges().forEach(ch => { if (ch.type === "added") onSig(ch.doc.data()); });
    }, () => {}));
    cur.unsubs.push(F.onSnapshot(ref, s => { if (!cur) return; const d = s.data(); if (!d || d.status === "ended") { toast("انتهت المكالمة"); leave(true); return; } cur.data = d;
      for (const p of Object.keys(cur.peers)) if (!(d.in || []).includes(p)) drop(p); onChange(); }, () => {}));
    const inside = (data.in || []).filter(x => x !== u.uid);
    await F.updateDoc(ref, {in:F.arrayUnion(u.uid)});
    for (const p of inside) await offerTo(p);
    onChange();
  }
  function peer(p){
    if (cur.peers[p]) return cur.peers[p];
    const pc = new RTCPeerConnection(ICE), o = {pc, dc:null, audio:null};
    if (cur.local) cur.local.getTracks().forEach(t => pc.addTrack(t, cur.local));
    if (cur.data.kind === "remote" && !cur.isHost) pc.addTransceiver("video", {direction:"recvonly"});
    pc.onicecandidate = e => { if (e.candidate) send(p, "ice", JSON.stringify(e.candidate)); };
    pc.ontrack = e => { const st = e.streams[0];
      if (e.track.kind === "video") { cur.remoteVideo = st; onChange(); }
      else { if (!o.audio) { o.audio = new Audio(); o.audio.autoplay = true; } o.audio.srcObject = st; o.audio.play().catch(() => {}); } };
    pc.ondatachannel = e => { o.dc = e.channel; wireDC(p, o.dc); };
    pc.onconnectionstatechange = () => { if (["failed", "closed"].includes(pc.connectionState)) drop(p); onChange(); };
    cur.peers[p] = o; return o;
  }
  function wireDC(p, dc){ dc.onmessage = e => { if (cur && cur.isHost && cur.data.kind === "remote" && PS && PS.remoteInput) { try { PS.remoteInput(cur.key, JSON.parse(e.data)); } catch(er) {} } }; }
  async function offerTo(p){ const o = peer(p); if (cur.data.kind === "remote") { o.dc = o.pc.createDataChannel("input"); wireDC(p, o.dc); }
    const off = await o.pc.createOffer(); await o.pc.setLocalDescription(off); send(p, "offer", JSON.stringify(off)); }
  async function onSig(m){
    if (!cur) return; const o = peer(m.from), d = JSON.parse(m.data);
    try {
      if (m.type === "offer") { await o.pc.setRemoteDescription(d); const ans = await o.pc.createAnswer(); await o.pc.setLocalDescription(ans); send(m.from, "answer", JSON.stringify(ans)); }
      else if (m.type === "answer") await o.pc.setRemoteDescription(d);
      else if (m.type === "ice") await o.pc.addIceCandidate(d);
    } catch(e) {}
  }
  function send(to, type, data){ const u = me(); if (!cur || !u) return; F.addDoc(F.collection(fs, "calls", cur.id, "sig"), {from:u.uid, to, type, data, at:F.serverTimestamp()}).catch(() => {}); }
  function drop(p){ const o = cur && cur.peers[p]; if (!o) return; try { o.pc.close(); } catch(e) {} if (o.audio) o.audio.srcObject = null; delete cur.peers[p]; }
  async function leave(silent){
    if (!cur) return; const c = cur; cur = null;
    c.unsubs.forEach(f => f()); Object.values(c.peers).forEach(o => { try { o.pc.close(); } catch(e) {} if (o.audio) o.audio.srcObject = null; });
    if (c.local) c.local.getTracks().forEach(t => t.stop());
    const u = me();
    if (!silent && u) { const left = (c.data.in || []).filter(x => x !== u.uid);
      try { await F.updateDoc(F.doc(fs, "calls", c.id), left.length && c.data.kind === "party" ? {in:F.arrayRemove(u.uid)} : {status:"ended", in:F.arrayRemove(u.uid)}); } catch(e) {} }
    onChange();
  }
  function decline(id){ const u = me(); delete incoming[id]; F.updateDoc(F.doc(fs, "calls", id), {status:"ended"}).catch(() => {}); onChange(); }
  function mute(){ if (!cur || !cur.local) return; cur.muted = !cur.muted; cur.local.getAudioTracks().forEach(t => t.enabled = !cur.muted); onChange(); }
  // viewer of Remote Play: send keys and mouse to the host
  function input(ev){ if (!cur) return; for (const o of Object.values(cur.peers)) if (o.dc && o.dc.readyState === "open") o.dc.send(JSON.stringify(ev)); }
  return {start, join, leave, decline, mute, input, watchIncoming, get cur(){ return cur; }, incoming, set onChange(f){ onChange = f; }};
}
