// Firestore rules tests (run in CI with the emulator)
import { initializeTestEnvironment, assertSucceeds, assertFails } from '@firebase/rules-unit-testing';
import { readFileSync } from 'fs';
import { doc, setDoc, getDoc, getDocs, collection, writeBatch, serverTimestamp, updateDoc, increment, query, where, deleteDoc } from 'firebase/firestore';
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
// developers' project library (BLOCKFALL Developer edition)
await env.withSecurityRulesDisabled(async c => { const d = c.firestore();
  await setDoc(doc(d, 'users/A/library/blockfall__dev'), { game: 'blockfall', edition: 'dev', status: 'granted', price: 0 });
  await setDoc(doc(d, 'users/B/library/blockfall__dev'), { game: 'blockfall', edition: 'dev', status: 'granted', price: 0 }); });
const proj = (o) => Object.assign({ kind: 'dungeon', key: 'k', name: 'مشروع', desc: '', data: '{}', ver: '00.00.69', byName: 'alice', updated: new Date(), public: false, ownerId: 'A', ownerName: 'alice', members: { A: { name: 'alice', role: 'owner' } }, memberIds: ['A'], rev: 1, created: new Date() }, o);
await ok('developer saves a project', assertSucceeds(setDoc(doc(A, 'bfDevProjects/p1'), proj())));
await ok('non-developer cannot save projects', assertFails(setDoc(doc(C, 'bfDevProjects/p2'), proj({ ownerId: 'C', memberIds: ['C'], members: { C: { name: 'carl', role: 'owner' } } }))));
await ok('cannot create a project for someone else', assertFails(setDoc(doc(A, 'bfDevProjects/p3'), proj({ ownerId: 'B', memberIds: ['B'] }))));
await ok('private project hidden from others', assertFails(getDoc(doc(B, 'bfDevProjects/p1'))));
await ok('member adds a developer', assertSucceeds(updateDoc(doc(A, 'bfDevProjects/p1'), { 'members.B': { name: 'bob', role: 'editor' }, memberIds: ['A', 'B'] })));
await ok('member reads and edits it', assertSucceeds((async () => { await getDoc(doc(B, 'bfDevProjects/p1')); await updateDoc(doc(B, 'bfDevProjects/p1'), { data: '{"x":1}', rev: 2, updated: new Date() }); })()));
await ok('member cannot remove the owner', assertFails(updateDoc(doc(B, 'bfDevProjects/p1'), { memberIds: ['B'] })));
await ok('member cannot take ownership', assertFails(updateDoc(doc(B, 'bfDevProjects/p1'), { ownerId: 'B' })));
await ok('member cannot delete', assertFails(deleteDoc(doc(B, 'bfDevProjects/p1'))));
await ok('max 20 developers', assertFails(updateDoc(doc(A, 'bfDevProjects/p1'), { memberIds: ['A', 'B', ...Array.from({ length: 19 }, (_, i) => 'x' + i)] })));
await ok('member can leave', assertSucceeds(updateDoc(doc(B, 'bfDevProjects/p1'), { memberIds: ['A'] })));
await ok('public project visible to everyone signed in', assertSucceeds((async () => { await updateDoc(doc(A, 'bfDevProjects/p1'), { public: true }); await getDocs(query(collection(C, 'bfDevProjects'), where('public', '==', true))); })()));
await ok('project list by member', assertSucceeds(getDocs(query(collection(A, 'bfDevProjects'), where('memberIds', 'array-contains', 'A')))));
await ok('owner deletes', assertSucceeds(deleteDoc(doc(A, 'bfDevProjects/p1'))));
// BLOCKFALL official content
const anon = env.unauthenticatedContext().firestore();
const ptr = (by) => ({ rev: 1, parts: 1, ver: '00.00.72', seq: 1, top: 1, hist: [], by: 'x', byId: by, at: new Date(), note: 'n', size: 10 });
await ok('studio publishes official content', assertSucceeds((async () => { await setDoc(doc(ADM, 'bfOfficial/blockfall_r1_0'), { data: '{}', rev: 1, i: 0 }); await setDoc(doc(ADM, 'bfOfficial/blockfall'), ptr('ADMIN')); })()));
await ok('anyone reads it without signing in', assertSucceeds((async () => { await getDoc(doc(anon, 'bfOfficial/blockfall')); await getDoc(doc(anon, 'bfOfficial/blockfall_r1_0')); })()));
await ok('a developer who is not a publisher cannot publish', assertFails(setDoc(doc(A, 'bfOfficial/blockfall'), ptr('A'))));
await ok('studio adds a publisher', assertSucceeds(setDoc(doc(ADM, 'settings/bfPublishers'), { uids: ['B'] })));
await ok('players cannot add themselves', assertFails(setDoc(doc(A, 'settings/bfPublishers'), { uids: ['A', 'B'] })));
await ok('publisher publishes', assertSucceeds((async () => { await setDoc(doc(B, 'bfOfficial/blockfall_r2_0'), { data: 'x'.repeat(1000), rev: 2, i: 0 }); await setDoc(doc(B, 'bfOfficial/blockfall'), Object.assign(ptr('B'), { rev: 2 })); })()));
await ok('part name must match its rev', assertFails(setDoc(doc(B, 'bfOfficial/blockfall_r9_0'), { data: '', rev: 3, i: 0 })));
await ok('part max 700 KB', assertFails(setDoc(doc(B, 'bfOfficial/blockfall_r3_0'), { data: 'x'.repeat(700001), rev: 3, i: 0 })));
await ok('official content is never deleted', assertFails(deleteDoc(doc(ADM, 'bfOfficial/blockfall_r1_0'))));
// sales
await ok('players cannot start a sale', assertFails(setDoc(doc(A, 'sales/blockfall'), { pct: 50, ends: new Date(Date.now() + 864e5), title: '', at: serverTimestamp() })));
await ok('no sale price without a sale', assertFails(setDoc(doc(B, 'users/B/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 69.99 * 75 / 100, status: 'pending', sale: true, at: serverTimestamp() })));
await ok('studio starts a sale', assertSucceeds(setDoc(doc(ADM, 'sales/last-recording'), { pct: 25, ends: new Date(Date.now() + 864e5), title: 'عرض', at: serverTimestamp() })));
await ok('anyone sees sales', assertSucceeds(getDoc(doc(env.unauthenticatedContext().firestore(), 'sales/last-recording'))));
await ok('buy at the sale price', assertSucceeds(setDoc(doc(B, 'users/B/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 69.99 * (100 - 25) / 100, status: 'pending', sale: true, at: serverTimestamp() })));
await ok('not below the sale price', assertFails(setDoc(doc(C, 'users/C/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 1, status: 'pending', sale: true, at: serverTimestamp() })));
await ok('sale over: no sale price', assertFails((async () => { await env.withSecurityRulesDisabled(c => setDoc(doc(c.firestore(), 'sales/last-recording'), { pct: 25, ends: new Date(Date.now() - 1000), title: '', at: new Date() }));
  await setDoc(doc(C, 'users/C/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 69.99 * 75 / 100, status: 'pending', sale: true, at: serverTimestamp() }); })()));
await ok('sale over 90% refused', assertFails(setDoc(doc(ADM, 'sales/blockfall'), { pct: 95, ends: new Date(Date.now() + 864e5), title: '', at: serverTimestamp() })));
// install on your PC from your phone
await ok('ask your PC to install a game', assertSucceeds(setDoc(doc(A, 'users/A/meta/remote'), { install: 'blockfall--gold', at: serverTimestamp() })));
await ok('the request must name a real edition', assertFails(setDoc(doc(A, 'users/A/meta/remote'), { install: 'blockfall--free', at: serverTimestamp() })));
await ok('nobody can ask your PC to install', assertFails(setDoc(doc(B, 'users/A/meta/remote'), { install: 'blockfall--gold', at: serverTimestamp() })));
// Premium Plus
const soon = d => new Date(Date.now() + d * 864e5);
await ok('plus: ask for a subscription at its price', assertSucceeds(setDoc(doc(C, 'users/C/plusOrders/o1'), { tier: 'essential', months: 12, price: 79.99, status: 'pending', at: serverTimestamp() })));
await ok('plus: not at a wrong price', assertFails(setDoc(doc(C, 'users/C/plusOrders/o2'), { tier: 'premium', months: 12, price: 1, status: 'pending', at: serverTimestamp() })));
await ok('plus: no made-up plan', assertFails(setDoc(doc(C, 'users/C/plusOrders/o3'), { tier: 'gold', months: 2, price: 1, status: 'pending', at: serverTimestamp() })));
await ok('plus: player cannot turn it on', assertFails(setDoc(doc(C, 'users/C/meta/plus'), { tier: 'premium', until: soon(365) })));
await ok('plus: player cannot mark the order paid', assertFails(updateDoc(doc(C, 'users/C/plusOrders/o1'), { status: 'active' })));
await ok('plus: studio sets the games', assertSucceeds(setDoc(doc(ADM, 'config/plus'), { monthly: ['last-recording__std'], catalog: { 'blockfall__std': 'extra', 'last-recording__gold': 'extra', 'blockfall__gold': 'premium' }, discount: 10, at: serverTimestamp() })));
await ok('plus: player cannot change the games', assertFails(setDoc(doc(C, 'config/plus'), { monthly: ['blockfall__gold'], catalog: {}, discount: 90, at: serverTimestamp() })));
await ok('plus: no monthly game without a subscription', assertFails(setDoc(doc(C, 'users/C/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 0, status: 'plus', tier: 'essential', at: serverTimestamp() })));
await ok('plus: studio confirms and turns it on', assertSucceeds((async () => { const b = writeBatch(ADM); b.update(doc(ADM, 'users/C/plusOrders/o1'), { status: 'active', paidAt: serverTimestamp() }); b.set(doc(ADM, 'users/C/meta/plus'), { tier: 'essential', until: soon(365), since: serverTimestamp() }); await b.commit(); })()));
await ok('plus: member adds the monthly game', assertSucceeds(setDoc(doc(C, 'users/C/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 0, status: 'plus', tier: 'essential', at: serverTimestamp() })));
await ok('plus: essential cannot take catalog games', assertFails(setDoc(doc(C, 'users/C/library/blockfall__std'), { game: 'blockfall', edition: 'std', price: 0, status: 'plus', tier: 'extra', at: serverTimestamp() })));
await ok('plus: cannot pass a non-monthly game as monthly', assertFails(setDoc(doc(C, 'users/C/library/blockfall__std'), { game: 'blockfall', edition: 'std', price: 0, status: 'plus', tier: 'essential', at: serverTimestamp() })));
await ok('plus: member discount on a purchase', assertSucceeds(setDoc(doc(C, 'users/C/library/blockfall__gold'), { game: 'blockfall', edition: 'gold', price: 99.99 * (100 - 10) / 100, status: 'pending', plus: true, at: serverTimestamp() })));
await ok('plus: no member discount for non-members', assertFails(setDoc(doc(B, 'users/B/library/blockfall__gold'), { game: 'blockfall', edition: 'gold', price: 99.99 * (100 - 10) / 100, status: 'pending', plus: true, at: serverTimestamp() })));
await ok('plus: extra member takes a catalog game', assertSucceeds((async () => { await env.withSecurityRulesDisabled(c => setDoc(doc(c.firestore(), 'users/B/meta/plus'), { tier: 'extra', until: soon(30) }));
  await setDoc(doc(B, 'users/B/library/last-recording__gold'), { game: 'last-recording', edition: 'gold', price: 0, status: 'plus', tier: 'extra', at: serverTimestamp() }); })()));
await ok('plus: a game you already have is not taken again', assertFails(setDoc(doc(B, 'users/B/library/blockfall__std'), { game: 'blockfall', edition: 'std', price: 0, status: 'plus', tier: 'extra', at: serverTimestamp() })));
await ok('plus: extra cannot take premium-only games', assertFails(setDoc(doc(B, 'users/B/library/blockfall__gold'), { game: 'blockfall', edition: 'gold', price: 0, status: 'plus', tier: 'premium', at: serverTimestamp() })));
await ok('plus: ended subscription takes nothing', assertFails((async () => { await env.withSecurityRulesDisabled(c => setDoc(doc(c.firestore(), 'users/A/meta/plus'), { tier: 'premium', until: new Date(Date.now() - 1000) }));
  await setDoc(doc(A, 'users/A/library/last-recording__std'), { game: 'last-recording', edition: 'std', price: 0, status: 'plus', tier: 'essential', at: serverTimestamp() }); })()));
await ok('plus: anyone can see the plus games', assertSucceeds(getDoc(doc(env.unauthenticatedContext().firestore(), 'config/plus'))));
// existing features still work
await ok('player still adds a demo', assertSucceeds(setDoc(doc(B, 'users/B/library/last-recording__demo'), { game: 'last-recording', edition: 'demo', price: 0, status: 'free', at: serverTimestamp() })));
await ok('player still renames', assertSucceeds(updateDoc(doc(B, 'users/B'), { name: 'Bobby' })));
await env.cleanup();
console.log(failed ? failed + ' FAILED' : 'ALL PASSED'); process.exit(failed ? 1 : 0);
