// BLOCKFALL 3001 — the world rules, exactly as the game runs them in its worker threads (game version 00.00.74).
// Generation (genNatural = the 20 cm near layer, genDeep = the 1 m layer, oreVeins, caves, biomes, cities) and meshing
// (buildGeoArrays = 20 cm blocks with AO and carved shapes, buildCoarseArrays = 1 m layer). wkMain at the end is the
// worker's message loop: it shows the inputs and outputs of each job. See NOTES.md for the data formats.

"use strict";
var seed=1,GENV=2,SPX=500,SPZ=500,WSZ=1000,GOX=0,GOZ=0,ORES=null,EBITS=null,MATS=[],TAVG=[],genMode=true;
const _bt=new Map(), _shp={};
const GPAL={"snow":[232,238,242],"desert":[214,192,134],"savanna":[176,164,92],"jungle":[44,104,40],"forest":[58,112,50],"plains":[104,150,68]};
const C=0.2;
const HC=100;
const CS=80;
const WATER=2.4;
const CELLS=640000;
const CY_MIN=-490;
const CY_MAX=1510;
const NCY=400;
const CBW=16;
const CCELLS=102400;
const BEDROCK=173;
const SNOWB=10;
const GRAVEL=19;
const CLAYB=20;
const STUMP=86;
const LIFT=28;
const ORE=62;
const DIAORE=87;
const COALORE=118;
const EARTH_W=8192;
const EARTH_H=4096;
const EM_X=4891.9697265625;
const EM_Y=4883.772216796875;
const CITY_Y=25;
const RIY={"lat":24.69,"lon":46.68,"R":15000};
const ROX=25233901.9932;
const ROZ=7258092.9645;
const RD=[[2000,22,"h"],[500,11,"s"],[100,5.5,"l"]];
const SIDEW=3;
const BTINT={"plains":[1,1,1],"forest":[0.78,0.92,0.68],"jungle":[0.55,1.08,0.5],"savanna":[1.45,1.12,0.52],"snow":[2.3,2.25,2.45],"desert":[1.3,1.1,0.6]};
const CHM=16;
const WMETERS=4000;
const GOLDORE=175;
const CUORE=177;
const LAPORE=179;
const REDORE=181;
const EMORE=183;
const DSLATE=146;
const TUFFB=148;
const OBSID=172;
const DGOLDORE=176;
const DREDORE=182;
const DLAPORE=180;
const DRB1=154;
const DRB2=152;
const DRB3=315;
function oreVeins(cx,cz){ const out=new Map(); if(!ORES) return out;
  for(let o=0;o<ORES.length;o++){ const e=ORES[o], n=e[2], sz=e[3]; if(!n||!sz||!e[0]) continue; const r=mulberry((seed^Math.imul(cx+7919,0x9e3779b1)^Math.imul(cz-7919,0x85ebca6b)^Math.imul(o+1,0xc2b2ae35))>>>0);
    for(let c=0;c<n;c++){ let X=(r()*16)|0, Z=(r()*16)|0; const u=e[6]?(r()+r())/2:r(); let Y=Math.floor(e[4]+(e[5]-e[4])*u+WATER);
      for(let k=0;k<sz;k++){ if(!out.has(X+','+Y+','+Z)) out.set(X+','+Y+','+Z,o); const d=(r()*6)|0; if(d===0) X++; else if(d===1) X--; else if(d===2) Z++; else if(d===3) Z--; else if(d===4) Y++; else Y--; X=X<0?0:X>15?15:X; Z=Z<0?0:Z>15?15:Z; } } }
  return out; }
function h2(x,y,s){ let h=Math.imul(x,374761393)+Math.imul(y,668265263)+Math.imul(s,982451653)|0; h=Math.imul(h^(h>>>13),1274126177); h^=h>>>16; return (h>>>0)/4294967296; }
function vn(x,y,s){ const xi=Math.floor(x),yi=Math.floor(y),xf=x-xi,yf=y-yi,u=xf*xf*(3-2*xf),v=yf*yf*(3-2*yf);
  const a=h2(xi,yi,s),b=h2(xi+1,yi,s),c=h2(xi,yi+1,s),d=h2(xi+1,yi+1,s); return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v; }
function fbm(x,y,s){ return vn(x,y,s)*0.6+vn(x*2.1,y*2.1,s+7)*0.28+vn(x*4.3,y*4.3,s+13)*0.12; }
function mulberry(a){ return ()=>{ a|=0; a=a+0x6D2B79F5|0; let t=Math.imul(a^a>>>15,1|a); t=t+Math.imul(t^t>>>7,61|t)^t; return ((t^t>>>14)>>>0)/4294967296; }; }
const smooth=(a,b,x)=>{const t=Math.min(1,Math.max(0,(x-a)/(b-a)));return t*t*(3-2*t);};
function hashf(x,y,z){ let h=Math.imul(x|0,73856093)^Math.imul(y|0,19349663)^Math.imul(z|0,83492791); h=Math.imul(h^(h>>>13),1274126177); h^=h>>>16; return (h>>>0)/4294967296; }
function terrainCells(x,z){
  if((GENV==='flat1'||GENV==='dge')) return 25;
  if(GENV==='ocean'){ const mx=(x+0.5)*C, mz=(z+0.5)*C, f=fbm(mx*0.006,mz*0.006,seed+91), far=Math.hypot(mx-SPX,mz-SPZ); let h=2+Math.round(fbm(mx*0.03,mz*0.03,seed+3)*1.6);
    if(f>0.74&&far>90){ const k=Math.min(1,(f-0.74)/0.06); h=Math.round(h+(15-h)*k+(vn(mx*.2,mz*.2,seed+8)-.5)*2*k); } return Math.max(2,h); }   // the endless sea: a sandy floor, a rare little island
  if(isEarth()) return earthCells(x,z);
  if(GENV==='flat'){ const dx=Math.abs((x+0.5)*C-SPX), dz=Math.abs((z+0.5)*C-SPZ); return dx<36&&dz<36?22:(dx<39&&dz<39?13:6); }
  if(GENV==='dragon'){ const mx=(x+0.5)*C, mz=(z+0.5)*C, d=Math.hypot(mx-SPX,mz-SPZ); let h=22+(fbm(mx*.018,mz*.018,seed+4)-.5)*34+Math.max(0,vn(mx*.07,mz*.07,seed+9)-.62)*120+Math.max(0,vn(mx*.25,mz*.25,seed+13)-.8)*40;
    h=h+(20-h)*smooth(85,55,d); return Math.max(6,Math.min(HC-30,Math.round(h))); }                       // the dragon's land: black crags around a flat arena
  let h=Math.min(deepOn()?HC:HC-30,Math.max(3,Math.round(terrainH(x,z)/C)));
  const ps=portalSite(); if(ps&&!ps.ret){ const d=Math.max(Math.abs((x+0.5)*C-ps.x),Math.abs((z+0.5)*C-ps.z)); if(d<36) h=Math.round(h+(ps.y-h)*smooth(36,26,d)); }   // the portal's ground is levelled
  return h;
}
function terrainH(x,z){
  const mx=(x+0.5)*C, mz=(z+0.5)*C, sd=seed;
  let h=4.4+(fbm(mx*0.012,mz*0.012,sd)-0.5)*4.6+(fbm(mx*0.045,mz*0.045,sd+5)-0.5)*1.6+(vn(mx*0.4,mz*0.4,sd+3)-0.5)*0.3;
  const lk=fbm(mx*0.008,mz*0.008,sd+77); if(lk>0.66) h=h+(1.3-h)*smooth(0.66,0.74,lk);

  const e=Math.min(mx,mz,WSZ-mx,WSZ-mz); if(e<45) h=1+(h-1)*smooth(4,45,e);
  if(!deepOn()) return h; return Math.min((CY_MAX-5)*C,h+mountainM(mx,mz));
}
function portalSite(){ if(!(GENV===5||GENV==='flat1'||GENV==='dragon')) return null; const key=seed+'|'+GENV+'|'+WSZ+'|'+SPX+'|'+SPZ; if(portalSite.k===key) return portalSite.v; let v;
  if(GENV==='dragon') v={x:Math.round(SPX/16)*16,z:Math.round(SPZ/16)*16,y:20,ret:1};
  else { const r=mulberry((seed^0x7f4a7c15)>>>0), a=r()*6.283, maxD=Math.max(70,WSZ/2-56), d=Math.min(260,maxD)*(.62+r()*.38);
    const x=Math.max(48,Math.min(WSZ-48,Math.round((SPX+Math.cos(a)*d)/16)*16)), z=Math.max(48,Math.min(WSZ-48,Math.round((SPZ+Math.sin(a)*d)/16)*16));
    let y=25; if(GENV!=='flat1'){ let t=0; for(const [ox,oz] of [[0,0],[-8,-8],[8,-8],[-8,8],[8,8]]) t+=terrainH(Math.floor((x+ox)/C),Math.floor((z+oz)/C)); y=Math.round(t/5/C); y=Math.max(Math.ceil((WATER+1.2)/C),Math.min(y,60)); }
    v={x,z,y}; }
  portalSite.k=key; portalSite.v=v; return v; }
function inPortalZone(xm,zm){ const ps=portalSite(); return !!ps&&Math.abs(xm-ps.x)<32&&Math.abs(zm-ps.z)<32; }
function earthCells(x,z){ const gx=(x+.5)*C+GOX, gz=(z+.5)*C+GOZ; let h;
  const cw=isEvent()?cityW(gx,gz):0; if(cw>=1) return CITY_Y;
  const l=landAt(gx,gz)+(fbm(gx/2200,gz/2200,81)-.5)*.22;
  if(l<.5){ h=Math.max(.6,WATER-.35-(.5-l)*5); }
  else { const t=smooth(.5,.6,l), mt=Math.pow(Math.max(0,fbm(gx/40000,gz/40000,90)-.52)*2.1,1.6)*7;
    const base=4.4+(fbm(gx*0.012,gz*0.012,seed)-0.5)*4.6+(fbm(gx*0.045,gz*0.045,seed+5)-0.5)*1.6+(vn(gx*0.4,gz*0.4,seed+3)-0.5)*0.3+mt;
    h=WATER+.35+(base-(WATER+.35))*t; }
  if(cw>0) h=h*(1-cw)+CITY_Y*C*cw;
  return Math.min(HC-30,Math.max(3,Math.round(h/C))); }
function landAt(gx,gz){ earthMask(); const wx=gx+(fbm(gx/9000,gz/9000,77)-.5)*7000+(vn(gx/1400,gz/1400,78)-.5)*900, wz=gz+(fbm(gx/9000+31,gz/9000,79)-.5)*7000+(vn(gx/1400,gz/1400+9,80)-.5)*900;
  const fx=wx/EM_X-.5, fy=wz/EM_Y-.5, ix=Math.floor(fx), iy=Math.floor(fy), tx=fx-ix, ty=fy-iy;
  const a=eBit(ix,iy), b=eBit(ix+1,iy), c=eBit(ix,iy+1), d=eBit(ix+1,iy+1); const v=(a*(1-tx)+b*tx)*(1-ty)+(c*(1-tx)+d*tx)*ty; return v; }
const eBit=(px,py)=>{ px=((px%EARTH_W)+EARTH_W)%EARTH_W; if(py<0||py>=EARTH_H) return py<0?0:1; const k=py*EARTH_W+px; return (EBITS[k>>3]>>(k&7))&1; };
function earthBiome(gx,gz){ const lat=latOf(gz), lon=lonOf(gx), al=Math.abs(lat), n=fbm(gx/60000,gz/60000,301), n2=fbm(gx/25000,gz/25000,302);
  if(al>63||lat<-58) return 'snow';
  if(al>55) return n>.5?'snow':'forest';
  const sahara=lat>14&&lat<33&&lon>-17&&lon<62, aus=lat<-19&&lat>-32&&lon>117&&lon<145, swUS=lat>26&&lat<37&&lon>-117&&lon<-104, kala=lat<-18&&lat>-28&&lon>15&&lon<25, gobi=lat>38&&lat<46&&lon>90&&lon<112, atac=lat<-18&&lat>-28&&lon>-71&&lon<-68;
  if(sahara||aus||swUS||kala||gobi||atac) return n2>.78?'savanna':'desert';
  if(al<10) return n2<.3?'savanna':'jungle';
  if(al<18) return n2>.55?'jungle':'savanna';
  if(al<35) return n>.62?'desert':n2>.5?'forest':'plains';
  return n2>.45?'forest':'plains'; }
const lonOf=gx=>gx/111319.49-180;
const latOf=gz=>90-gz/111132.95;
const gxOf=lon=>(lon+180)*111319.49;
const gzOf=lat=>(90-lat)*111132.95;
function biomeAt(mx,mz){ if(GENV==='flat'||(GENV==='flat1'||GENV==='dge')) return 'plains'; if(isEarth()) return earthBiome(mx+GOX,mz+GOZ); let t=fbm(mx*0.0035,mz*0.0035,seed+101), h=fbm(mx*0.0035,mz*0.0035,seed+202);
  { const d=Math.hypot(mx-SPX,mz-SPZ), w=1-smooth(40,90,d); t=t+(0.48-t)*w; h=h+(0.5-h)*w; }   // start in green plains
  if(t<0.36) return 'snow'; if(t>0.6&&h<0.44) return 'desert'; if(t>0.56&&h<0.56) return 'savanna'; if(h>0.62) return t>0.52?'jungle':'forest'; return 'plains'; }
function biomeTint(x,z){ const k=(x>>3)*100000+(z>>3); let v=_bt.get(k); if(!v){ const b=biomeAt((x+0.5)*C,(z+0.5)*C); v=BTINT[b]; if(_bt.size>40000) _bt.clear(); _bt.set(k,v); } return v; }
function cityGround(gx,gz){ const d=Math.hypot(gx-ROX,gz-ROZ); if(d>RIY.R+400) return null; if(d<=RIY.R) return CITY_Y; return null; }
function cityW(gx,gz){ const d=Math.hypot(gx-ROX,gz-ROZ); return d<=RIY.R?1:d<RIY.R+400?1-(d-RIY.R)/400:0; }
function roadLine(u){ for(const [P,hw,k] of RD){ const m=((u%P)+P)%P, d=Math.min(m,P-m); if(d<=hw+SIDEW) return {d,hw,k,P}; } return null; }
function edgeHW(u){ for(const [P,hw] of RD){ const m=((u%P)+P)%P; if(Math.min(m,P-m)<0.01) return hw; } return 5.5; }
function cityTop(gx,gz){ const u=gx-ROX, v=gz-ROZ, a=roadLine(u), b=roadLine(v);
  const inA=a&&a.d<=a.hw, inB=b&&b.d<=b.hw;
  if(inA||inB){ const r=inA&&inB?null:inA?a:b, along=inA?v:u;
    if(!r) return 23;                                               // junction
    const o=inA?b:a; if(o&&o.d<=o.hw+SIDEW+4&&o.d>o.hw){ return (Math.floor(along*2.5)%2===0)?10:23; }   // zebra crossing
    const d=r.d, dash=((along%8)+8)%8<4;
    if(r.k==='h'){ if(d<1) return 19; if(Math.abs(d-1.3)<.12) return 24; for(const L of [4.9,8.4,11.9,15.4,18.9]) if(Math.abs(d-L)<.12&&dash) return 10; if(Math.abs(d-(r.hw-.6))<.12) return 10; return 23; }
    if(r.k==='s'){ if(d<.25) return 24; if(Math.abs(d-5.5)<.12&&dash) return 10; return 23; }
    if(d<.12&&dash) return 10; return 23; }
  if((a&&a.d<=a.hw+SIDEW)||(b&&b.d<=b.hw+SIDEW)){ const dd=a&&a.d<=a.hw+SIDEW?a.d-a.hw:b.d-b.hw; return dd<.35?10:19; }   // kerb then sidewalk
  return 4; }
const isEarth=()=>GENV===3||GENV===4;
const isEvent=()=>GENV===4;
const deepOn=()=>GENV===5;
function mountainM(mx,mz){ if(!deepOn()) return 0; const sd=seed;
  const mask=smooth(.5,.66,fbm(mx*.0017,mz*.0017,sd+500)); if(mask<=0) return 0;
  const r=1-Math.abs(2*fbm(mx*.0055,mz*.0055,sd+501)-1);
  let h=(Math.pow(r,2.3)*84+fbm(mx*.02,mz*.02,sd+502)*16+(vn(mx*.09,mz*.09,sd+503)-.5)*3)*mask;
  h*=smooth(140,260,Math.hypot(mx-SPX,mz-SPZ)); h*=smooth(40,120,Math.min(mx,mz,WSZ-mx,WSZ-mz));
  return Math.max(0,h); }
function caveAir(mx,my,mz,surf){ const sd=seed; if(my<CY_MIN*C+3) return false;
  const ent=vn(mx*.012,mz*.012,sd+610)>.8;
  if(my>surf-(ent?0:3.5)) return false;
  if(surf<WATER+1.3&&my>-14) return false;
  if(Math.hypot(mx-SPX,mz-SPZ)<24&&my>-12) return false;
  const a=vn3(mx/26,my/17,mz/26,sd+611)-.5, b=vn3(mx/26,my/17,mz/26,sd+612)-.5; if(a*a+b*b<(ent?.0028:.0021)) return true;
  if(my<-10){ const c=vn3(mx/64,my/30,mz/64,sd+613)*.72+vn3(mx/22,my/12,mz/22,sd+614)*.28; if(c>.715) return true; }
  if(my<-40){ const d=vn3(mx/40,my/40,mz/40,sd+615)-.5; if(Math.abs(d)<.012&&vn3(mx/90,my/90,mz/90,sd+616)>.55) return true; }   // ravine sheets
  return false; }
function h3(x,y,z,s){ let h=Math.imul(x,374761393)+Math.imul(y,668265263)+Math.imul(z,1440662683)+Math.imul(s,982451653)|0; h=Math.imul(h^(h>>>13),1274126177); h^=h>>>16; return (h>>>0)/4294967296; }
function vn3(x,y,z,s){ const xi=Math.floor(x),yi=Math.floor(y),zi=Math.floor(z),xf=x-xi,yf=y-yi,zf=z-zi,u=xf*xf*(3-2*xf),v=yf*yf*(3-2*yf),w=zf*zf*(3-2*zf), L=(a,b,t)=>a+(b-a)*t;
  return L(L(L(h3(xi,yi,zi,s),h3(xi+1,yi,zi,s),u),L(h3(xi,yi+1,zi,s),h3(xi+1,yi+1,zi,s),u),v),L(L(h3(xi,yi,zi+1,s),h3(xi+1,yi,zi+1,s),u),L(h3(xi,yi+1,zi+1,s),h3(xi+1,yi+1,zi+1,s),u),v),w); }
const cIdx=(X,Y,Z)=>X+CBW*(Z+CBW*Y);
const bandCave=(ch,x,y,z)=>ch.cav&&y<HC&&ch.cav[(((x-ch.x0)/5)|0)+CBW*((((z-ch.z0)/5)|0)+CBW*((y/5)|0))]===1;
function deepPrep(ch){ const x0=ch.x0, z0=ch.z0, top=new Float32Array(CBW*CBW);
  for(let Z=0;Z<CBW;Z++)for(let X=0;X<CBW;X++){ const xc=x0+X*5+2, zc=z0+Z*5+2; top[X+CBW*Z]=deepOn()?terrainH(xc,zc):terrainCells(xc,zc)*C; }
  ch.ctop=top; ch.cav=null;
  if(deepOn()&&!isEvent()){ const cav=new Uint8Array(CBW*CBW*(HC/5)); let any=0;
    for(let Yb=0;Yb<HC/5;Yb++)for(let Z=0;Z<CBW;Z++)for(let X=0;X<CBW;X++){ const s=top[X+CBW*Z], my=Yb+.5; if(my>s+.5) continue;
      if(caveAir((x0+X*5+2.5)*C,my,(z0+Z*5+2.5)*C,s)){ cav[X+CBW*(Z+CBW*Yb)]=1; any=1; } }
    if(any) ch.cav=cav; } }
function genDeep(ch){ const OV=oreVeins(Math.floor(ch.x0/CS),Math.floor(ch.z0/CS)), g=ch.gc, x0=ch.x0, z0=ch.z0, sd=seed, top=ch.ctop, R=mulberry(Math.imul(ch.cx+3,73517)^Math.imul(ch.cz+11,31337)^sd), flat=GENV==='flat'||GENV==='flat1'||GENV==='dge', city=isEvent(), bandTop=HC*C;
  for(let Z=0;Z<CBW;Z++)for(let X=0;X<CBW;X++){ const s=top[X+CBW*Z], mx=(x0+X*5+2.5)*C, mz=(z0+Z*5+2.5)*C;
    let steep=0; if(s>bandTop){ for(const [dx,dz] of [[1,0],[-1,0],[0,1],[0,-1]]){ const X2=X+dx, Z2=Z+dz; const t2=(X2>=0&&Z2>=0&&X2<CBW&&Z2<CBW)?top[X2+CBW*Z2]:terrainH(x0+X2*5+2,z0+Z2*5+2); steep=Math.max(steep,Math.abs(t2-s)); } }
    for(let Y=0;Y<NCY;Y++){ const yc=CY_MIN+Y*5; if(yc>=0&&yc<HC) continue; const my=yc*C+.5;   // centre of the 1 m block (m)
      let m=0;
      if(yc<0){ // underground
        if(Y<3&&(Y===0||R()<(Y===1?.6:.25))) m=BEDROCK;
        else if(!flat&&!city&&caveAir(mx,my,mz,s)) m=0;
        else { m=3; const q=h3(X+x0,Y,Z+z0,sd+77);
          if(!flat){ const bl=vn3(mx/9,my/7,mz/9,sd+620); if(bl>.8) m=my>-30?2:GRAVEL; else if(bl<.12&&my>-50) m=CLAYB;
            const deep=my<-50; if(deep&&m===3) m=DSLATE; else if(q>.975&&my<-35&&my>-62&&m===3) m=TUFFB;
            // ores come in veins like Minecraft (see oreVeins): only stone, deepslate and tuff turn into ore
            const o=(m===3||m===DSLATE||m===TUFFB)?OV.get(X+','+Math.floor(my)+','+Z):undefined;
            if(o!==undefined&&!ORES[o][7]) m=(m===DSLATE&&ORES[o][1])?ORES[o][1]:ORES[o][0];
            else if(my<-84&&q<.5&&vn3(mx/2.1,my/2.1,mz/2.1,sd+700)<.06) m=OBSID; } } }
      else if(my<s){ // mountains above the band
        if(caveAir(mx,my,mz,s)) m=0;
        else { const d=s-my; m=steep>2.2?3:d<1?(my>bandTop+50?SNOWB:1):d<4?2:3; if(m===3){ const o=OV.get(X+','+Math.floor(my)+','+Z); if(o!==undefined) m=ORES[o][0]; } } }
      g[cIdx(X,Y,Z)]=m; } }
  ch.cdirty=true; }
function genNatural(ch){
  deepPrep(ch);
  const sd=seed, x0=ch.x0, z0=ch.z0, R=mulberry(Math.imul(ch.cx+1,92821)^Math.imul(ch.cz+7,68917)^sd);
  const hc=new Int16Array(CS*CS);
  for(let z=0;z<CS;z++)for(let x=0;x<CS;x++) hc[x+z*CS]=terrainCells(x0+x,z0+z);
  const H=(x,z)=>hc[(x-x0)+(z-z0)*CS]; const inCityC=isEvent()&&cityW(x0*C+GOX+CHM/2,z0*C+GOZ+CHM/2)>0;
  const rocky=(x,z)=>fbm(x*C*0.018,z*C*0.018,sd+21);
  const meadow=(x,z)=>vn(x*C*0.024,z*C*0.024,sd+40);
  const NB=CS/5, baseY=new Int16Array(NB*NB);
  for(let Z=0;Z<NB;Z++)for(let X=0;X<NB;X++){
    let mn=1e9; for(let z=Z*5;z<Z*5+5;z++)for(let x=X*5;x<X*5+5;x++) mn=Math.min(mn,hc[x+z*CS]);
    const nb=(GENV==='flat1'||GENV==='dge')?Math.floor(mn/5):Math.max(0,Math.floor((mn-1)/5)); baseY[X+Z*NB]=nb*5;
    const sandy=mn*C<WATER+0.6;
    for(let y=0;y<nb;y++){ if(bandCave(ch,x0+X*5,y*5,z0+Z*5)) continue; let m=(GENV==='flat1'||GENV==='dge')?(y===nb-1?1:y>=nb-3?2:3):y===nb-1?(sandy?4:2):(sandy&&y>=nb-2?4:(R()<0.15?19:3)); if(GENV>=2&&m===3){ R(); const OV=ch.__ov||(ch.__ov=oreVeins(Math.floor(x0/CS),Math.floor(z0/CS))), o=OV.get(X+','+y+','+Z); if(o!==undefined&&!ORES[o][7]) m=ORES[o][0]; } addBlock(x0+X*5,y*5,z0+Z*5,5,m); }
  }
  let maxH=0; for(let i=0;i<hc.length;i++) maxH=Math.max(maxH,hc[i]);
  const g=ch.g;
  const need=(x,y,z)=>y>=baseY[((x-x0)/5|0)+((z-z0)/5|0)*NB]&&y<H(x,z)&&!bandCave(ch,x,y,z);
  const occ=(x,y,z)=>g[(x-x0)+CS*((z-z0)+CS*y)];
  const fits=(x,y,z,s)=>{ if(x+s>x0+CS||z+s>z0+CS) return false;
    for(let yy=y;yy<y+s;yy++)for(let zz=z;zz<z+s;zz++)for(let xx=x;xx<x+s;xx++) if(!need(xx,yy,zz)||occ(xx,yy,zz)) return false; return true; };
  for(let y=0;y<maxH;y++)for(let z=z0;z<z0+CS;z++)for(let x=x0;x<x0+CS;x++){
    if(!need(x,y,z)||occ(x,y,z)) continue;
    let smax=1; for(let s=2;s<=4;s++){ if(fits(x,y,z,s)) smax=s; else break; }
    let s=smax; if(smax>1){ const r=R(); s=r<0.4?smax:r<0.75?smax-1:1+(R()*smax|0); }
    let top=false; for(let zz=z;zz<z+s&&!top;zz++)for(let xx=x;xx<x+s;xx++) if(H(xx,zz)<=y+s){top=true;break;}
    let cm=0; if(inCityC&&top){ const gxx=(x+.5)*C+GOX, gzz=(z+.5)*C+GOZ; if(cityW(gxx,gzz)>=1){ cm=cityTop(gxx,gzz);
        if(s>1){ let uni=true; for(let zz=z;zz<z+s&&uni;zz++)for(let xx=x;xx<x+s;xx++) if(cityTop((xx+.5)*C+GOX,(zz+.5)*C+GOZ)!==cm){ uni=false; break; } if(!uni){ s=1; top=H(x,z)<=y+1; } } } }
    const hh=H(x,z)*C; let m;
    const bio=biomeAt(x*C,z*C);
    if(GENV==='dragon') m=top?(R()<.05?DRB3:R()<.55?DRB1:DRB2):(R()<.4?DRB2:3); else if(cm&&top) m=cm; else if(inCityC&&cityW((x+.5)*C+GOX,(z+.5)*C+GOZ)>=1) m=y+s>=H(x,z)-2?4:3;
    else if(hh<WATER+0.5||bio==='desert'&&y+s>=H(x,z)-3) m=4;
    else if(!inCityC&&rocky(x,z)>0.72) m=R()<0.5?3:19;
    else if(top) m=1;
    else m=y+s>=H(x,z)-2?2:(R()<0.3?20:2);
    addBlock(x,y,z,s,m);
  }
  const topAt=(x,z,s)=>{ let t=0; for(let zz=z;zz<z+s;zz++)for(let xx=x;xx<x+s;xx++) t=Math.max(t,H(xx,zz)); return t; };
  const inC=(x,z,s)=>x>=x0&&z>=z0&&x+s<=x0+CS&&z+s<=z0+CS;
  const tryAdd=(x,y,z,s,m)=>{ x=Math.round(x); y=Math.round(y); z=Math.round(z); if(inC(x,z,s)&&canPlace(x,y,z,s)){ addBlock(x,y,z,s,m); return true; } return false; };
  const center=(cx,y,cz,s,m)=>tryAdd(Math.round(cx-s/2),y,Math.round(cz-s/2),s,m);
  const nearSpawn=(x,z)=>!isEarth()&&Math.hypot(x*C-SPX,z*C-SPZ)<2.5;
  const okLand=(x,z)=>H(x,z)*C>WATER+0.7&&!nearSpawn(x,z)&&!inPortalZone(x*C,z*C)&&biomeAt(x*C,z*C)!=='desert';
  const rp=(mg)=>[x0+mg+(R()*(CS-2*mg)|0), z0+mg+(R()*(CS-2*mg)|0)];
  // spawn lift
  if(GENV!=='flat'&&GENV!=='flat1'&&GENV!=='ocean'&&!isEarth()){ const lx=Math.round((SPX+2.5)/C), lz=Math.round((SPZ-0.5)/C); if(inC(lx,lz,5)) addBlock(lx,topAt(lx,lz,5),lz,5,LIFT,true); }
  for(const QD of [[0,0],[1,0],[0,1],[1,1]]){ const QX=x0+QD[0]*40, QZ=z0+QD[1]*40; const rp=(mg)=>[QX+mg+(R()*(40-2*mg)|0), QZ+mg+(R()*(40-2*mg)|0)];
  const forest=fbm((QX+20)*C*0.01,(QZ+20)*C*0.01,sd+50);
  const cb=biomeAt((QX+20)*C,(QZ+20)*C), bmul={plains:0.8,forest:1.8,jungle:2.4,savanna:0.35,snow:0.6,desert:0}[cb];
  const nt=(GENV==='flat'||(GENV==='flat1'||GENV==='dge')||GENV==='dragon'||inCityC||inPortalZone(x0*C+8,z0*C+8))?-1:Math.max(0,Math.floor(((forest-0.42)*14+R()*0.9+(cb==='forest'||cb==='jungle'?2:0))*bmul));
  if(GENV>=2&&nt>=0){
    // trees of many sizes: shrub, small, medium, large, giant (broadleaf or pine)
    const SZ=[{ts:1,n:[4,6],cr:4,bs:2},{ts:1,n:[7,10],cr:6,bs:3},{ts:2,n:[4,6],cr:8,bs:4},{ts:3,n:[5,7],cr:11,bs:5},{ts:4,n:[6,8],cr:14,bs:5}];
    const blob=(cx,cy,cz,rad,bs,m,squash)=>{ for(let dy=-rad;dy<=rad;dy+=bs)for(let dz=-rad;dz<=rad;dz+=bs)for(let dx=-rad;dx<=rad;dx+=bs){
      const d=(dx*dx+dz*dz)/(rad*rad)+(dy*dy)/(rad*rad*squash*squash); if(d>1||(d>0.6&&R()<0.35)) continue;
      const b=Math.max(1,bs-(d>0.7&&R()<0.5?1:0)); center(cx+dx+(R()-0.5),cy+dy,cz+dz+(R()-0.5),b,m); } };
    for(let n=0,nn=nt+(R()<0.3?1:0);n<nn;n++){
      const r=R(), k=r<0.14?0:r<0.38?1:r<0.72?2:r<0.93?3:4, S=SZ[k];
      const [x,z]=rp(k>=3?S.cr+2:Math.max(6,S.cr)); if(!okLand(x,z)||rocky(x,z)>0.72) continue;
      const y0=topAt(x,z,S.ts); if(!canPlace(x,y0,z,S.ts)) continue;
      const pine=k>0&&R()<0.4, cnt=S.n[0]+(R()*(S.n[1]-S.n[0]+1)|0);
      let h=0; for(let i=0;i<cnt;i++){ if(!tryAdd(x,y0+i*S.ts,z,S.ts,i===0?STUMP:5)) break; h++; }
      if(!h) continue;
      const cx=x+S.ts/2, cz=z+S.ts/2, ty=y0+h*S.ts;
      if(pine){ const L=Math.max(3,Math.round(S.cr*0.9/S.bs)+1);
        for(let j=0;j<L;j++){ const rad=Math.max(S.bs*0.5,S.cr*(1-j/L)), y=ty-S.bs*1.5+j*S.bs; const b=Math.max(1,S.bs-(j>L-3?1:0));
          for(let dz=-rad;dz<=rad;dz+=b)for(let dx=-rad;dx<=rad;dx+=b) if(dx*dx+dz*dz<=rad*rad) center(cx+dx,y,cz+dz,b,27); }
        center(cx,ty-S.bs*1.5+L*S.bs,cz,1,27); }
      else { center(cx,ty,cz,Math.min(5,S.bs+1),6); blob(cx,ty+S.cr*0.25,cz,S.cr,S.bs,6,0.75); if(k>=3) for(let q=0;q<3;q++){ const a=R()*6.28; blob(cx+Math.cos(a)*S.cr*0.7,ty-S.cr*0.1,cz+Math.sin(a)*S.cr*0.7,S.cr*0.5,S.bs,6,0.7); } }
    }
  } else
  for(let n=0;n<nt;n++){
    const [x,z]=rp(8); if(!okLand(x,z)||rocky(x,z)>0.72) continue;
    const y0=topAt(x,z,2); if(!canPlace(x,y0,z,2)) continue;
    const cx=x+1, cz=z+1;
    if(R()<0.45){
      const k=4+(R()*3|0); for(let i=0;i<k;i++) tryAdd(x,y0+i*2,z,2,5);
      const ty=y0+k*2;
      center(cx,ty,cz,5,27); for(const [a,b] of [[1,0],[-1,0],[0,1],[0,-1]]) center(cx+a*4,ty,cz+b*4,3,27);
      center(cx,ty+5,cz,4,27); center(cx,ty+9,cz,3,27); center(cx,ty+12,cz,2,27); center(cx,ty+14,cz,1,27);
    } else {
      const k=5+(R()*3|0); for(let i=0;i<k;i++) tryAdd(x,y0+i*2,z,2,5);
      const ty=y0+k*2;
      center(cx,ty,cz,5,6);
      for(const [a,b] of [[1,0],[-1,0],[0,1],[0,-1]]) center(cx+a*4.5,ty-2+(R()*2|0),cz+b*4.5,4,6);
      center(cx,ty+5,cz,3,6);
      for(const [a,b] of [[1,1],[1,-1],[-1,1],[-1,-1]]) if(R()<0.7) center(cx+a*4,ty+(R()*3|0)+1,cz+b*4,2,6);
      for(let i=0;i<4;i++) center(cx+(R()-0.5)*12,ty-3+(R()*8|0),cz+(R()-0.5)*12,1,6);
    }
  }
  if(!(GENV==='flat'||(GENV==='flat1'||GENV==='dge')||GENV==='dragon'||inCityC||inPortalZone(x0*C+8,z0*C+8))&&R()<0.3+(rocky(QX+20,QZ+20)>0.66?0.5:0)){
    const [x,z]=rp(6); if(okLand(x,z)){ const c=2+(R()*4|0);
      for(let i=0;i<c;i++){ const s=1+(R()*4|0), px=x+((R()-0.5)*8|0), pz=z+((R()-0.5)*8|0); if(!inC(px,pz,s)) continue;
        const y=topAt(px,pz,s); if(tryAdd(px,y,pz,s,GENV>=2&&R()<0.18?ORE:R()<0.6?3:19)&&R()<0.35&&s>1) tryAdd(px+(R()*(s-1)|0),y+s,pz+(R()*(s-1)|0),Math.max(1,s-2),3); } }
  }
  if(!(GENV==='flat'||(GENV==='flat1'||GENV==='dge')||GENV==='dragon'||inCityC||inPortalZone(x0*C+8,z0*C+8))&&R()<0.45){
    const [x,z]=rp(5); if(okLand(x,z)){ const c=2+(R()*3|0); for(let i=0;i<c;i++){ const s=2+(R()*2|0), px=x+((R()-0.5)*6|0), pz=z+((R()-0.5)*6|0); if(!inC(px,pz,s)) continue; tryAdd(px,topAt(px,pz,s)+(i>2?2:0),pz,s,6); } }
  }
  const fl=[15,16,29,30], nf=(GENV==='flat'||(GENV==='flat1'||GENV==='dge')||GENV==='dragon'||inCityC||inPortalZone(x0*C+8,z0*C+8))?0:12+(R()*30|0);
  for(let n=0;n<nf;n++){
    const [x,z]=rp(0); if(H(x,z)*C<WATER+0.5||rocky(x,z)>0.72) continue;
    tryAdd(x,H(x,z),z,1,n%4===0?fl[(R()*fl.length)|0]:18);
  }
  }
}
function buildGeoArrays(list,get,kindOf){
  const pos=[],nor=[],col=[],bu=[],tl=[],ao=[],IG=[[],[],[],[],[],[],[]]; let vc=0, cur=6; const q=[0,0,0],q2=[0,0,0];
  const open=id=>id===0||(id>0&&kindOf(id)!==0);
  const solid=(x,y,z)=>{ const id=get(x,y,z); return id<0||(id>0&&kindOf(id)===0)?1:0; };
  for(const b of list){
    if(b.k===2||b.k===8) continue;
    if(b.sh){ const sz=b.s*C, ox=b.x*C, oy=b.y*C, oz=b.z*C, rr0=b.r||0, Mw=MATS[b.m];
      const tlt=rr0>>2; const tr=(x,y,z)=>{ let X=x-.5, Y=y-.5, Z=z-.5; if(tlt===1){ Y=-Y; X=-X; } else if(tlt===2){ const t=Y; Y=-Z; Z=t; } y=Y+.5; for(let i=0;i<(rr0&3);i++){ const t=X; X=Z; Z=-t; } return [ox+(X+.5)*sz, oy+y*sz, oz+(Z+.5)*sz]; };
      const cen=tr(.5,.33,.33);
      const tri=(a,b2,c,tile)=>{ let A=tr(...a),Bv=tr(...b2),Cv=tr(...c); const u=[Bv[0]-A[0],Bv[1]-A[1],Bv[2]-A[2]], w=[Cv[0]-A[0],Cv[1]-A[1],Cv[2]-A[2]];
        let n=[u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0]]; const l=Math.hypot(...n)||1; n=n.map(v=>v/l);
        if(n[0]*(A[0]-cen[0])+n[1]*(A[1]-cen[1])+n[2]*(A[2]-cen[2])<0){ const t=Bv; Bv=Cv; Cv=t; n=n.map(v=>-v); }
        for(const Q of [A,Bv,Cv]){ pos.push(Q[0],Q[1],Q[2]); nor.push(n[0],n[1],n[2]); col.push(...TAVG[tile]); bu.push(.5,.5,b.s); tl.push(tile); ao.push(3); }
        IG[6].push(vc,vc+1,vc+2); vc+=3; };
      const quad=(a,b2,c,d,tile)=>{ tri(a,b2,c,tile); tri(a,c,d,tile); };
      const T0=Mw.t?Mw.t[0]:0, T1=Mw.t?Mw.t[1]:0, T2=Mw.t?Mw.t[2]:0;
      if(b.sh===1){ quad([0,0,0],[1,0,0],[1,0,1],[0,0,1],T2); quad([0,0,0],[1,0,0],[1,1,0],[0,1,0],T1); quad([0,1,0],[1,1,0],[1,0,1],[0,0,1],T0);
        tri([0,0,0],[0,0,1],[0,1,0],T1); tri([1,0,0],[1,0,1],[1,1,0],T1); continue; }
      const sf=shapeTris(b.sh); let cx0=0,cy0=0,cz0=0; for(const t of sf) for(const p of t){ cx0+=p[0]; cy0+=p[1]; cz0+=p[2]; } const nn=sf.length*3||1;
      const cc=tr(cx0/nn,cy0/nn,cz0/nn); cen[0]=cc[0]; cen[1]=cc[1]; cen[2]=cc[2];
      for(const t of sf){ const A=tr(...t[0]),Bp=tr(...t[1]),Cp=tr(...t[2]); const u=[Bp[0]-A[0],Bp[1]-A[1],Bp[2]-A[2]], w=[Cp[0]-A[0],Cp[1]-A[1],Cp[2]-A[2]]; const ny=(u[2]*w[0]-u[0]*w[2])/(Math.hypot(u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0])||1);
        const sgn=((u[1]*w[2]-u[2]*w[1])*(A[0]-cen[0])+(u[2]*w[0]-u[0]*w[2])*(A[1]-cen[1])+(u[0]*w[1]-u[1]*w[0])*(A[2]-cen[2]))<0?-1:1; const yy=ny*sgn;
        tri(t[0],t[1],t[2],yy>0.6?T0:yy<-0.6?T2:T1); }
      continue; }
    const M=MATS[b.m], t=b.t;
    if(b.k===1){
      const e=b.s*C, x0=b.x*C, y0=b.y*C, z0=b.z*C, n=e*0.08, tile=M.t[1];
      for(const [ax1,az1,bx1,bz1] of [[x0+n,z0+n,x0+e-n,z0+e-n],[x0+n,z0+e-n,x0+e-n,z0+n]]){
        const cs=[[ax1,y0,az1,0,0],[bx1,y0,bz1,1,0],[bx1,y0+e,bz1,1,1],[ax1,y0+e,az1,0,1]];
        for(const c of cs){ pos.push(c[0],c[1],c[2]); nor.push(0,1,0); col.push(...TAVG[tile]); bu.push(c[3],c[4],-1); tl.push(tile); ao.push(c[4]?3:2); }
        IG[6].push(vc,vc+1,vc+2,vc,vc+2,vc+3, vc,vc+2,vc+1,vc,vc+3,vc+2); vc+=4;
      }
      continue;
    }
    const s=b.s, o=[b.x,b.y,b.z];
    for(let a=0;a<3;a++){ const u=(a+1)%3,v=(a+2)%3;
      for(let sg=1;sg>=-1;sg-=2){
        cur=a*2+(sg>0?0:1); const Lr=sg>0?o[a]+s:o[a]-1; q[a]=Lr; let ex=0;
        for(let i=0;i<s;i++){q[u]=o[u]+i;for(let j=0;j<s;j++){q[v]=o[v]+j; if(open(get(q[0],q[1],q[2]))) ex++;}}
        if(!ex) continue;
        const p=sg>0?o[a]+s:o[a];
        const tile=a===1?(sg>0?M.t[0]:M.t[2]):M.t[1];
        const n=[0,0,0]; n[a]=sg;
        const g2=(uu,vv)=>{ q2[a]=Lr; q2[u]=uu; q2[v]=vv; return solid(q2[0],q2[1],q2[2]); };
        const emit=(u0,u1,v0,v1)=>{
          const cs=sg>0?[[u0,v0],[u1,v0],[u1,v1],[u0,v1]]:[[u0,v0],[u0,v1],[u1,v1],[u1,v0]];
          const av=[];
          for(const c of cs){
            const w=[0,0,0]; w[a]=p; w[u]=c[0]; w[v]=c[1];
            pos.push(w[0]*C,w[1]*C,w[2]*C); nor.push(n[0],n[1],n[2]); col.push(...(tile===0||tile===1||tile===13||tile===14?biomeTint(o[0],o[2]):TAVG[tile])); tl.push(tile);
            if(a===0) bu.push((w[2]-o[2])/s,(w[1]-o[1])/s,s); else if(a===2) bu.push((w[0]-o[0])/s,(w[1]-o[1])/s,s); else bu.push((w[0]-o[0])/s,(w[2]-o[2])/s,s);
            const iu=c[0]===u0?u0:u1-1, ou=c[0]===u0?u0-1:u1, iv=c[1]===v0?v0:v1-1, ov=c[1]===v0?v0-1:v1;
            const s1=g2(ou,iv), s2=g2(iu,ov), cc=g2(ou,ov); const val=(s1&&s2)?0:3-(s1+s2+cc); av.push(val); ao.push(val);
          }
          if(av[0]+av[2]<av[1]+av[3]) IG[cur].push(vc+1,vc+2,vc+3,vc+1,vc+3,vc); else IG[cur].push(vc,vc+1,vc+2,vc,vc+2,vc+3);
          vc+=4;
        };
        if(ex===s*s) emit(o[u],o[u]+s,o[v],o[v]+s);
        else for(let i=0;i<s;i++){q[u]=o[u]+i;for(let j=0;j<s;j++){q[v]=o[v]+j; q[a]=Lr; if(open(get(q[0],q[1],q[2]))) emit(o[u]+i,o[u]+i+1,o[v]+j,o[v]+j+1);}}
      }
    }
  }
  const ind=[], ranges=[]; for(const A of IG){ ranges.push([ind.length,A.length]); for(let i=0;i<A.length;i++) ind.push(A[i]); }
  return {pos:new Float32Array(pos),nor:new Float32Array(nor),col:new Float32Array(col),bu:new Float32Array(bu),tl:new Float32Array(tl),ao:new Float32Array(ao),ind:vc>65535?new Uint32Array(ind):new Uint16Array(ind),ranges,vc};
}
function shapeTris(sh){ if(_shp[sh]) return _shp[sh]; const T=[];
  const prismY=(poly,y0,y1)=>{ const n=poly.length; for(let i=0;i<n;i++){ const a=poly[i], b=poly[(i+1)%n]; T.push([[a[0],y0,a[1]],[b[0],y0,b[1]],[b[0],y1,b[1]]],[[a[0],y0,a[1]],[b[0],y1,b[1]],[a[0],y1,a[1]]]); }
    for(let i=1;i<n-1;i++){ T.push([[poly[0][0],y0,poly[0][1]],[poly[i][0],y0,poly[i][1]],[poly[i+1][0],y0,poly[i+1][1]]],[[poly[0][0],y1,poly[0][1]],[poly[i][0],y1,poly[i][1]],[poly[i+1][0],y1,poly[i+1][1]]]); } };
  const prismX=(poly)=>{ const n=poly.length; for(let i=0;i<n;i++){ const a=poly[i], b=poly[(i+1)%n]; T.push([[0,a[0],a[1]],[1,a[0],a[1]],[1,b[0],b[1]]],[[0,a[0],a[1]],[1,b[0],b[1]],[0,b[0],b[1]]]); }
    for(let i=1;i<n-1;i++){ T.push([[0,poly[0][0],poly[0][1]],[0,poly[i][0],poly[i][1]],[0,poly[i+1][0],poly[i+1][1]]],[[1,poly[0][0],poly[0][1]],[1,poly[i][0],poly[i][1]],[1,poly[i+1][0],poly[i+1][1]]]); } };
  const ring=(k,r=.5)=>Array.from({length:k},(_,i)=>{ const a=i/k*Math.PI*2+(k===6?0:Math.PI/k); return [.5+Math.cos(a)*r,.5+Math.sin(a)*r]; });
  if(sh===2) prismY([[0,0],[1,0],[1,1],[0,1]],0,.5);
  else if(sh===3) prismY(ring(6),0,1);
  else if(sh===6) prismY(ring(14),0,1);
  else if(sh===4){ const p=[]; for(let i=0;i<=10;i++){ const a=i/10*Math.PI; p.push([Math.sin(a)*.5,.5+Math.cos(a)*.5]); } prismX(p); }
  else if(sh===5) prismX([[0,0],[.5,0],[0,1]]);
  else if(sh===8) prismX([[0,0],[1,.5],[0,1]]);
  else if(sh===7){ const A=[.5,1,.5], q=[[0,0,0],[1,0,0],[1,0,1],[0,0,1]]; for(let i=0;i<4;i++) T.push([q[i],q[(i+1)%4],A]); T.push([q[0],q[1],q[2]],[q[0],q[2],q[3]]); }
  else prismY([[0,0],[1,0],[1,1],[0,1]],0,1);
  return _shp[sh]=T; }
function buildCoarseArrays(ctx){ const S=ctx.S, tint=ctx.tint, x0=ctx.x0, z0=ctx.z0;
  // two parts: U = mountains above the band (casts shadows), L = underground (dark, hidden when you're far up on the surface)
  const mk=()=>({pos:[],nor:[],col:[],bu:[],tl:[],ao:[],dk:[],IG:[[],[],[],[],[],[],[]],vc:0}); const PU=mk(), PL=mk(); const g=ctx.gc, top=ctx.top;
  // highest 1 m block of every column, once per build (used for the cave darkness)
  const ctop=new Float32Array(CBW*CBW).fill(-1e9); for(let Z=0;Z<CBW;Z++)for(let X=0;X<CBW;X++){ for(let Y=NCY-1;Y>=0;Y--){ const yc=CY_MIN+Y*5; if(yc>=0&&yc<HC) continue; if(g[cIdx(X,Y,Z)]){ ctop[X+CBW*Z]=(yc+5)*C; break; } } }
  const o=[0,0,0], q=[0,0,0], r=[0,0,0], w=[0,0,0], av=[0,0,0,0];
  for(let Y=0;Y<NCY;Y++){ const yc=CY_MIN+Y*5; if(yc>=0&&yc<HC) continue; const Pp=yc<0?PL:PU;
    for(let Z=0;Z<CBW;Z++)for(let X=0;X<CBW;X++){ const m=g[cIdx(X,Y,Z)]; if(!m) continue; const M=MATS[m]; if(!M||!M.t) continue;
      o[0]=X; o[1]=Y; o[2]=Z;
      for(let a=0;a<3;a++){ const u=(a+1)%3, v=(a+2)%3;
        for(let sg=1;sg>=-1;sg-=2){ q[0]=X; q[1]=Y; q[2]=Z; q[a]+=sg; if(S(q[0],q[1],q[2])) continue;
          const tile=a===1?(sg>0?M.t[0]:M.t[2]):M.t[1];
          const tc=(tile===0||tile===1||tile===13||tile===14)?tint(x0+X*5,z0+Z*5):TAVG[tile];
          const qx=Math.max(0,Math.min(CBW-1,q[0])), qz=Math.max(0,Math.min(CBW-1,q[2])), airM=(CY_MIN+q[1]*5)*C+.5, ci=qx+CBW*qz;
          const cmax=Math.max(top[ci],ctop[ci]>airM?ctop[ci]:-1e9); const dark=Math.max(0,Math.min(.84,(cmax-airM-1.2)/9));
          const p=sg>0?o[a]+1:o[a], d=a*2+(sg>0?0:1), vc=Pp.vc;
          for(let k=0;k<4;k++){ const cu=(sg>0?(k===1||k===2):(k===2||k===3))?o[u]+1:o[u], cv=(sg>0?(k>=2):(k===1||k===2))?o[v]+1:o[v];
            w[a]=p; w[u]=cu; w[v]=cv;
            Pp.pos.push((x0+w[0]*5)*C,(CY_MIN+w[1]*5)*C,(z0+w[2]*5)*C); Pp.nor.push(a===0?sg:0,a===1?sg:0,a===2?sg:0); Pp.col.push(tc[0],tc[1],tc[2]); Pp.tl.push(tile); Pp.dk.push(dark);
            if(a===0) Pp.bu.push(w[2]-o[2],w[1]-o[1],5); else if(a===2) Pp.bu.push(w[0]-o[0],w[1]-o[1],5); else Pp.bu.push(w[0]-o[0],w[2]-o[2],5);
            const ou=cu===o[u]?o[u]-1:o[u]+1, ov=cv===o[v]?o[v]-1:o[v]+1;
            r[a]=q[a]; r[u]=ou; r[v]=o[v]; const s1=S(r[0],r[1],r[2]); r[u]=o[u]; r[v]=ov; const s2=S(r[0],r[1],r[2]); r[u]=ou; const cc=S(r[0],r[1],r[2]);
            const val=(s1&&s2)?0:3-(s1+s2+cc); av[k]=val; Pp.ao.push(val); }
          if(av[0]+av[2]<av[1]+av[3]) Pp.IG[d].push(vc+1,vc+2,vc+3,vc+1,vc+3,vc); else Pp.IG[d].push(vc,vc+1,vc+2,vc,vc+2,vc+3); Pp.vc+=4; } } } }
  const arr=P_=>{ if(!P_.vc) return null; const ind=[], ranges=[]; for(const A of P_.IG){ ranges.push([ind.length,A.length]); for(let i=0;i<A.length;i++) ind.push(A[i]); }
    return {pos:new Float32Array(P_.pos),nor:new Float32Array(P_.nor),col:new Float32Array(P_.col),bu:new Float32Array(P_.bu),tl:new Float32Array(P_.tl),ao:new Float32Array(P_.ao),dk:new Float32Array(P_.dk),ind:P_.vc>65535?new Uint32Array(ind):new Uint16Array(ind),ranges,vc:P_.vc}; };
  return {U:arr(PU),L:arr(PL)}; }
function globePixels(W,H){ const px=new Uint8ClampedArray(W*H*4);
  for(let y=0;y<H;y++){ const lat=90-(y+.5)/H*180, gz=gzOf(lat);
    for(let x=0;x<W;x++){ const lon=(x+.5)/W*360-180, gx=gxOf(lon), fx=Math.floor(gx/EM_X-.5), fy=Math.floor(gz/EM_Y-.5); let c;
      if(Math.abs(lat)>80) c=[226,234,240];
      else if(eBit(fx,fy)){ const b=GPAL[earthBiome(gx,gz)]||GPAL.plains, n=.86+vn(gx/60000,gz/60000,5)*.28; c=[b[0]*n,b[1]*n,b[2]*n]; }
      else { const near=eBit(fx+4,fy)+eBit(fx-4,fy)+eBit(fx,fy+4)+eBit(fx,fy-4); c=near?[38,104,160]:[16,48,104]; }
      const i=(x+W*y)*4; px[i]=c[0]; px[i+1]=c[1]; px[i+2]=c[2]; px[i+3]=255; } }
  return px; }
function earthMask(){ return EBITS; }
(function wkMain(){
  let cfgKey='';
  const setCfg=c=>{ const k=JSON.stringify(c); if(k===cfgKey) return; cfgKey=k; seed=c.seed; GENV=c.GENV; ORES=c.ORES||null; SPX=c.SPX; SPZ=c.SPZ; WSZ=c.WSZ; GOX=c.GOX; GOZ=c.GOZ; _bt.clear(); };
  let ch=null, rec=null;
  self.addBlock=(x,y,z,s,m,p,o)=>{ const lid=ch.b.length; if(lid>65535) return 0; const sy=(o&&o.sy)||s;
    if(x<ch.x0||z<ch.z0||x+s>ch.x0+CS||z+s>ch.z0+CS||y<0||y+sy>HC) return 0;
    ch.b.push(1); rec.push(x,y,z,s,m,p?1:0,sy,(o&&o.r)||0); const sv=MATS[m]&&MATS[m].k===0&&m!==6&&m!==27&&!MATS[m].tr?1:0;
    for(let yy=y;yy<y+sy;yy++)for(let zz=z;zz<z+s;zz++){ const base=(zz-ch.z0)*CS+CS*CS*yy-ch.x0; ch.g.fill(lid,base+x,base+x+s); ch.sol.fill(sv,base+x,base+x+s); } return lid; };
  self.canPlace=(x,y,z,s,sy)=>{ sy=sy||s; if(y<0||y+sy>HC||x<ch.x0||z<ch.z0||x+s>ch.x0+CS||z+s>ch.z0+CS) return false;
    for(let yy=y;yy<y+sy;yy++)for(let zz=z;zz<z+s;zz++){ const base=(zz-ch.z0)*CS+CS*CS*yy-ch.x0; for(let xx=x;xx<x+s;xx++) if(ch.g[base+xx]) return false; } return true; };
  self.onmessage=e=>{ const d=e.data, T0=performance.now(); try{
    if(d.type==='init'){ MATS=d.mats; TAVG=d.tavg; return; }
    if(d.type==='ebits'){ EBITS=d.ebits; return; }
    if(d.cfg) setCfg(d.cfg);
    if(d.type==='gen'){
      ch={cx:d.cx,cz:d.cz,x0:d.cx*CS,z0:d.cz*CS,g:new Uint16Array(CELLS),sol:new Uint8Array(CELLS),b:[null],gc:new Uint16Array(CCELLS)}; rec=[];
      genNatural(ch); genDeep(ch);
      const r=new Int32Array(rec), gc=ch.gc, ctop=ch.ctop, cav=ch.cav, g=ch.g, sol=ch.sol; ch=null; rec=null;
      const tr=[r.buffer,gc.buffer,ctop.buffer,g.buffer,sol.buffer]; if(cav) tr.push(cav.buffer);
      postMessage({id:d.id,type:'gen',rec:r,gc,ctop,cav,g,sol,ms:performance.now()-T0},tr); return; }
    if(d.type==='mesh'){
      const R_=d.rec, list=[]; for(let i=0;i<R_.length;i+=10) list.push({x:R_[i],y:R_[i+1],z:R_[i+2],s:R_[i+3],sy:R_[i+4],m:R_[i+5],k:R_[i+6],sh:R_[i+7],r:R_[i+8],t:R_[i+9]});
      const W=CS+6, x0=d.x0, z0=d.z0, P=new Uint8Array(W*W*(HC+2)), O=d.own, Bd=d.bord; let k=0;
      for(let pz=0;pz<W;pz++)for(let px=0;px<W;px++){ if(px>=1&&px<=CS&&pz>=1&&pz<=CS) continue; const o=k*HC; k++; for(let y=0;y<HC;y++) P[px+W*(pz+W*(y+1))]=Bd[o+y]; }
      for(let y=0;y<HC;y++)for(let lz=0;lz<CS;lz++){ const si=CS*(lz+CS*y); P.set(O.subarray(si,si+CS),1+W*((lz+1)+W*(y+1))); }
      const pl=(W*W-CS*CS)*HC; P.set(Bd.subarray(pl,pl+W*W),0); P.set(Bd.subarray(pl+W*W,pl+2*W*W),W*W*(HC+1));
      const get=(x,y,z)=>{ const px=x-x0+1, pz=z-z0+1; if(px<0||pz<0||px>=W||pz>=W||y<-1||y>HC) return -1; return P[px+W*(pz+W*(y+1))]?-1:0; };
      const A=buildGeoArrays(list,get,()=>1);
      postMessage({id:d.id,type:'mesh',A,ms:performance.now()-T0},[A.pos.buffer,A.nor.buffer,A.col.buffer,A.bu.buffer,A.tl.buffer,A.ao.buffer,A.ind.buffer]); return; }
    if(d.type==='globe'){ const px=globePixels(d.W,d.H); postMessage({id:d.id,type:'globe',px:px.buffer,ms:performance.now()-T0},[px.buffer]); return; }
    if(d.type==='cmesh'){
      const Q=d.sol, W=CBW+2;
      const S=(X,Y,Z)=>{ if(Y<0) return 1; if(Y>=NCY) return 0; if(X<-1||Z<-1||X>CBW||Z>CBW) return 1; return Q[(X+1)+W*((Z+1)+W*Y)]; };
      const R_=buildCoarseArrays({S,tint:biomeTint,x0:d.x0,z0:d.z0,gc:d.gc,top:d.top}); const tr=[];
      for(const k of ['U','L']) if(R_[k]) for(const a of ['pos','nor','col','bu','tl','ao','dk','ind']) tr.push(R_[k][a].buffer);
      postMessage({id:d.id,type:'cmesh',R:R_,ms:performance.now()-T0},tr); return; }
  }catch(err){ postMessage({id:d.id,type:'err',msg:String(err&&err.stack||err)}); } };
})();
