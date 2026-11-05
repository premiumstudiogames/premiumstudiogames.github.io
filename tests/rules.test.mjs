// Firestore rules tests (run in CI with the emulator)
import { initializeTestEnvironment, assertSucceeds, assertFails } from '@firebase/rules-unit-testing';
import { readFileSync } from 'fs';
import { doc, setDoc, getDoc, getDocs, collection, writeBatch, serverTimestamp, updateDoc, increment, query, where } from 'firebase/firestore';
const env = await initializeTestEnvironment({ projectId: 'premium-studio-test', firestore: { rules: readFileSync('firestore.rules', 'utf8'), host: '127.0.0.1', port: 8080 } });
let failed = 0; const ok = async (name, p) => { try { await p; console.log('PASS', name); } catch (e) { failed++; console.log('FAIL', name, e.message.slice(0, 300)); } };
await env.withSecurityRulesDisabled(async c => {
  const d = c.firestore();
  for (const [u, n] of [['A', 'alice'], ['B', 'bob'], ['C', 'carl']]) await setDoc(doc(d, 'users', u), { pid: '000000000' + u.charCodeAt(0) % 10, username: n, name: n, coins: 1000 });
  await setDoc(doc(d, 'users/A/friends/B'), { username: 'bob', status: 'ok' }); await setDoc(doc(d, 'users/B/friends/A'), { username: 'alice', status: 'ok' });
  await setDoc(doc(d, 'users/A/library/blockfall__gold'), { game: 'blockfall', edition: 'gold', status: 'granted', price: 0 });
  await setDoc(doc(d, 'users/A/play/blockfall'), { game: 'blockfall', minutes: 65 });
  for (let i = 1; i <= 5; i++) await setDoc(doc(d, `users/C/cards/blockfall_${i}`), { game: 'blockfall', count: 1, via: 'drop' });
});
const A = env.authenticatedContext('A').firestore(), B = env.authenticatedContext('B').firestore(), C = env.authenticatedContext('C').firestore();
const ADM = env.authenticatedContext('ADMIN', { email: 'abdullra7manx@gmail.com', email_verified: true }).firestore();
// gifts
await ok('gift to a friend', assertSucceeds(setDoc(doc(A, 'users/A/gifts/g1'), { to: 'B', toName: 'bob', game: 'blockfall', edition: 'std', price: 39.99, status: 'pending', at: serverTimestamp() })));
await ok('no gift to a stranger', assertFails(setDoc(doc(A, 'users/A/gifts/g2'), { to: 'C', toName: 'carl', game: 'blockfall', edition: 'std', price: 39.99, status: 'pending', at: serverTimestamp() })));
await ok('no gift at a wrong price', assertFails(setDoc(doc(A, 'users/A/gifts/g3'), { to: 'B', toName: 'bob', game: 'blockfall', edition: 'std', price: 1, status: 'pending', at: serverTimestamp() })));
await ok('studio sends the gift', assertSucceeds((async () => { const b = writeBatch(ADM); b.set(doc(ADM, 'users/B/library/blockfall__std'), { game: 'blockfall', edition: 'std', price: 0, status: 'granted', at: serverTimestamp(), from: 'alice' }); b.update(doc(ADM, 'users/A/gifts/g1'), { status: 'sent', sentAt: serverTimestamp() }); await b.commit(); })()));
// family
await ok('owner shares with a friend', assertSucceeds(setDoc(doc(A, 'families/A'), { members: ['B'], names: { B: 'bob' } })));
await ok('family member reads the shared library', assertSucceeds(getDocs(collection(B, 'users/A/library'))));
await ok('others cannot read it', assertFails(getDocs(collection(C, 'users/A/library'))));
await ok('max 5 members', assertFails(setDoc(doc(A, 'families/A'), { members: ['1', '2', '3', '4', '5', '6'], names: {} })));
// card drops
const drop = (db, n) => { const b = writeBatch(db); b.set(doc(db, 'users/A/cardDrops/blockfall'), { n }); b.set(doc(db, `users/A/cards/blockfall_${n}`), { game: 'blockfall', count: 1, via: 'drop' }); return b.commit(); };
await ok('card drop after 30 min', assertSucceeds(drop(A, 1)));
await ok('second drop after 60 min', assertSucceeds(drop(A, 2)));
await ok('no third drop before 90 min', assertFails(drop(A, 3)));
await ok('cannot give yourself cards', assertFails(setDoc(doc(A, 'users/A/cards/blockfall_4'), { game: 'blockfall', count: 5, via: 'buy', lastTrade: 'x' })));
// market: A lists a card, B buys
await ok('list a card', assertSucceeds((async () => { const b = writeBatch(A); b.set(doc(A, 'listings/L1'), { seller: 'A', sellerName: 'alice', card: 'blockfall_1', game: 'blockfall', price: 250, status: 'open', at: serverTimestamp() });
  b.update(doc(A, 'users/A/cards/blockfall_1'), { count: 0, via: 'list', lastTrade: 'L1' }); await b.commit(); })()));
await ok('cannot list a card you do not have', assertFails((async () => { const b = writeBatch(A); b.set(doc(A, 'listings/L9'), { seller: 'A', sellerName: 'alice', card: 'blockfall_5', game: 'blockfall', price: 1, status: 'open', at: serverTimestamp() });
  b.set(doc(A, 'users/A/cards/blockfall_5'), { game: 'blockfall', count: 0, via: 'list', lastTrade: 'L9' }); await b.commit(); })()));
const buy = (db, me, extra) => { const b = writeBatch(db); b.update(doc(db, 'listings/L1'), { status: 'sold', buyer: me, soldAt: serverTimestamp() });
  b.update(doc(db, `users/${me}`), { coins: 1000 - 250 + (extra || 0), lastTrade: 'L1' }); b.update(doc(db, 'users/A'), { coins: 1000 + 250, lastSale: 'L1' });
  b.set(doc(db, `users/${me}/cards/blockfall_1`), { game: 'blockfall', count: 1, via: 'buy', lastTrade: 'L1' }); return b.commit(); };
await ok('cannot buy cheaper than the price', assertFails(buy(B, 'B', 100)));
await ok('buy a card', assertSucceeds(buy(B, 'B')));
await ok('cannot buy it twice', assertFails(buy(C, 'C')));
await ok('cannot just raise your coins', assertFails(updateDoc(doc(B, 'users/B'), { coins: 999999, lastTrade: 'L1' })));
// cancel
await ok('list another and take it back', assertSucceeds((async () => {
  let b = writeBatch(A); b.set(doc(A, 'listings/L2'), { seller: 'A', sellerName: 'alice', card: 'blockfall_2', game: 'blockfall', price: 10, status: 'open', at: serverTimestamp() }); b.update(doc(A, 'users/A/cards/blockfall_2'), { count: 0, via: 'list', lastTrade: 'L2' }); await b.commit();
  b = writeBatch(A); b.update(doc(A, 'listings/L2'), { status: 'cancelled' }); b.update(doc(A, 'users/A/cards/blockfall_2'), { count: 1, via: 'cancel', lastTrade: 'L2' }); await b.commit(); })()));
// the way the store does it: seller paid with increment(), family found with a query
await ok('buy with increment (store code)', assertSucceeds((async () => {
  let b = writeBatch(A); b.set(doc(A, 'listings/L3'), { seller: 'A', sellerName: 'alice', card: 'blockfall_2', game: 'blockfall', price: 40, status: 'open', at: serverTimestamp() }); b.update(doc(A, 'users/A/cards/blockfall_2'), { count: 0, via: 'list', lastTrade: 'L3' }); await b.commit();
  const bal = (await getDoc(doc(B, 'users/B'))).data().coins;
  b = writeBatch(B); b.update(doc(B, 'listings/L3'), { status: 'sold', buyer: 'B', soldAt: serverTimestamp() }); b.update(doc(B, 'users/B'), { coins: bal - 40, lastTrade: 'L3' });
  b.update(doc(B, 'users/A'), { coins: increment(40), lastSale: 'L3' }); b.set(doc(B, 'users/B/cards/blockfall_2'), { game: 'blockfall', count: 1, via: 'buy', lastTrade: 'L3' }); await b.commit(); })()));
await ok('family query by member', assertSucceeds(getDocs(query(collection(B, 'families'), where('members', 'array-contains', 'B')))));
// badge
const craft = db => { const b = writeBatch(db); for (let i = 1; i <= 5; i++) b.update(doc(db, `users/C/cards/blockfall_${i}`), { count: 0, via: 'craft' }); b.set(doc(db, 'users/C/badges/blockfall'), { level: 1, at: serverTimestamp() }); return b.commit(); };
await ok('craft a badge from a full set', assertSucceeds(craft(C)));
await ok('no badge without cards', assertFails((async () => { const b = writeBatch(C); b.set(doc(C, 'users/C/badges/blockfall'), { level: 2, at: serverTimestamp() }); await b.commit(); })()));
// voice calls
await ok('call a friend', assertSucceeds(setDoc(doc(A, 'calls/K1'), { host: 'A', members: ['A', 'B'], names: { A: 'alice', B: 'bob' }, kind: 'voice', game: '', in: ['A'], status: 'live', at: serverTimestamp() })));
await ok('cannot call a stranger', assertFails(setDoc(doc(A, 'calls/K2'), { host: 'A', members: ['A', 'C'], names: {}, kind: 'voice', game: '', in: ['A'], status: 'live', at: serverTimestamp() })));
await ok('friend sees the call', assertSucceeds(getDocs(query(collection(B, 'calls'), where('members', 'array-contains', 'B')))));
await ok('stranger cannot see it', assertFails(getDoc(doc(C, 'calls/K1'))));
await ok('friend joins', assertSucceeds(updateDoc(doc(B, 'calls/K1'), { in: ['A', 'B'] })));
await ok('cannot add someone else', assertFails(updateDoc(doc(B, 'calls/K1'), { in: ['A', 'B', 'C'] })));
await ok('send a handshake', assertSucceeds(setDoc(doc(B, 'calls/K1/sig/s1'), { from: 'B', to: 'A', type: 'offer', data: '{}', at: serverTimestamp() })));
await ok('only the receiver reads it', assertSucceeds(getDocs(query(collection(A, 'calls/K1/sig'), where('to', '==', 'A')))));
await ok('outsider cannot send', assertFails(setDoc(doc(C, 'calls/K1/sig/s2'), { from: 'C', to: 'A', type: 'offer', data: '{}', at: serverTimestamp() })));
await ok('hang up', assertSucceeds(updateDoc(doc(A, 'calls/K1'), { status: 'ended', in: ['B'] })));
// workshop
await ok('upload a world', assertSucceeds((async () => { const b = writeBatch(A); b.set(doc(A, 'workshop/W1'), { game: 'blockfall', author: 'A', authorName: 'alice', title: 'قلعة', desc: '', mode: 'c', parts: 1, size: 10, subs: 0, at: serverTimestamp() }); b.set(doc(A, 'workshop/W1/parts/0'), { d: 'abc' }); await b.commit(); })()));
await ok('cannot add parts to someone else\'s world', assertFails(setDoc(doc(B, 'workshop/W1/parts/1'), { d: 'x' })));
await ok('subscribe counts +1', assertSucceeds(updateDoc(doc(B, 'workshop/W1'), { subs: increment(1) })));
await ok('cannot fake many subscribers', assertFails(updateDoc(doc(B, 'workshop/W1'), { subs: 99 })));
await ok('others read the world', assertSucceeds(getDoc(doc(C, 'workshop/W1/parts/0'))));
// devices and sign out everywhere
await ok('register a device', assertSucceeds(setDoc(doc(A, 'users/A/devices/d1'), { name: 'Windows', first: serverTimestamp(), last: serverTimestamp() })));
await ok('sign out everywhere', assertSucceeds(setDoc(doc(A, 'users/A/meta/security'), { signOutBefore: serverTimestamp() })));
await ok('nobody else sees your devices', assertFails(getDocs(collection(B, 'users/A/devices'))));
// existing features still work
await ok('player still adds a demo', assertSucceeds(setDoc(doc(B, 'users/B/library/last-recording__demo'), { game: 'last-recording', edition: 'demo', price: 0, status: 'free', at: serverTimestamp() })));
await ok('player still renames', assertSucceeds(updateDoc(doc(B, 'users/B'), { name: 'Bobby' })));
await env.cleanup();
console.log(failed ? failed + ' FAILED' : 'ALL PASSED'); process.exit(failed ? 1 : 0);
