#!/usr/bin/env python3
"""Procedural generic monster stand-ins. Deterministic; rerun to regenerate."""
import json, os
from PIL import Image
HERE=os.path.dirname(os.path.abspath(__file__))
FILL=(0x5C,0x63,0x70,255); MID=(0x8C,0x7A,0x62,255); OUT=(0x1E,0x22,0x28,255); SEAM=(0x82,0x82,0x82,255)
SIZES={"tiny":(16,16),"small":(24,32),"medium":(32,48),"large":(48,64)}
CLIPS=[("idle",4,6,"loop"),("walk",6,10,"loop"),("react",3,12,"once"),("attack",6,12,"once"),
("cast",4,10,"once"),("stun",4,8,"loop"),("death",5,8,"hold_last"),("fall",3,12,"loop"),("fall_impact",3,12,"once")]
FACINGS=["se","sw","ne","nw"]

def params(clip,f,n):
    """returns dx, lift(dy up), body w scale, h scale, head forward"""
    dx=lift=0; sw=sh=1.0; mid=False
    if clip=="idle": lift=[0,1,1,0][f]
    elif clip=="walk": lift=[0,1,2,1,0,0][f]; dx=[0,0,1,1,0,0][f]
    elif clip=="react": dx=[-1,-2,-1][f]; sh=[1,.92,.96][f]
    elif clip=="attack": dx=[0,-1,1,3,2,0][f]; sh=[1,.95,1,1,1,1][f]
    elif clip=="cast": sw=sh=[1,1.06,1.12,1][f]; mid=f in(1,2)
    elif clip=="stun": dx=[0,1,0,-1][f]
    elif clip=="death": sh=[1,.85,.65,.45,.3][f]; sw=[1,1.05,1.12,1.2,1.25][f]; dx=[0,-1,-1,-2,-2][f]
    elif clip=="fall": lift=[3,4,3][f]; dx=[0,1,-1][f]
    elif clip=="fall_impact": sh=[.7,.85,1][f]; sw=[1.2,1.08,1][f]
    return dx,lift,sw,sh,mid

def draw(size,facing,clip,f,n):
    W,H=SIZES[size]; px,py=W//2,H-1
    img=Image.new("RGBA",(W,H),(0,0,0,0)); p=img.load()
    dx,lift,sw,sh,mid=params(clip,f,n)
    north=facing.startswith("n")
    m=set()
    bw=max(3,int(W*0.36*sw)); bh=max(2,int(H*0.30*sh*(W/H*1.4 if H>W else 1)))
    bh=max(2,min(bh,H//2)); bw=min(bw,W//2-2)
    cx=px+dx; base=py-lift
    cy=base-bh  # ellipse bottom touches base
    for y in range(H):
        for x in range(W):
            if ((x-cx+.5)/bw)**2+((y-cy+.5)/bh)**2<=1: m.add((x,y))
    # legs/feet to pivot row
    lw=max(1,W//16)
    for lx in (cx-bw//2, cx+bw//2-lw):
        for y in range(cy,base+1):
            for x in range(lx,lx+lw): m.add((x,y))
    # head nub toward +x (se/ne tip), up for ne
    hr=max(2,bh*2//3 if size!="tiny" else 2)
    hx=cx+bw-hr//2+(1 if clip=="attack" and f==3 else 0); hy=cy-bh//2-(hr//2 if north else 0)
    if clip=="death" and f>=3: hy=cy
    for y in range(H):
        for x in range(W):
            if (x-hx+.5)**2+(y-hy+.5)**2<=hr*hr: m.add((x,y))
    # ensure pivot pixel unless airborne
    if lift==0: m.add((px,py))
    m={(x,y) for x,y in m if 1<=x<W-1 and 1<=y<H or (x,y)==(px,py)}
    for (x,y) in m: p[x,y]=FILL
    # mid tone: back patch (ne/nw shows back -> larger)
    for (x,y) in m:
        if y< cy-(0 if north else bh//2) and x<cx+bw//2: p[x,y]=MID if not mid else SEAM
    if mid:
        for (x,y) in m:
            if abs(x-cx)<=1: p[x,y]=MID
    # outline
    for (x,y) in m:
        if any((x+a,y+b) not in m for a,b in((1,0),(-1,0),(0,1),(0,-1))): p[x,y]=OUT
    if clip=="stun":  # wobble star
        sx=cx+[-2,0,2,0][f]; sy=max(0,cy-bh-hr-1)
        if 0<=sx<W: p[sx,sy]=SEAM
    if facing in("sw","nw"):
        # flip, then shift +1px so pivot x=W//2 maps onto itself (even widths); cols 0 & W-1 are kept empty pre-flip
        fl=img.transpose(Image.FLIP_LEFT_RIGHT); img=Image.new("RGBA",(W,H),(0,0,0,0)); img.paste(fl.crop((0,0,W-1,H)),(1,0))
    return img,lift

def main():
    man={"spec":"MONSTER_STANDIN_SPEC.md","clips":[]}; issues=[]
    for size,(W,H) in SIZES.items():
        for fc in FACINGS:
            d=os.path.join(HERE,"frames",size,fc); os.makedirs(d,exist_ok=True)
            for clip,n,fps,mode in CLIPS:
                paths=[]; frames=[]
                for f in range(n):
                    im,lift=draw(size,fc,clip,f,n)
                    a=im.getchannel("A"); assert im.size==(W,H)
                    assert set(a.tobytes())<= {0,255}
                    if lift==0 and a.getpixel((W//2,H-1))!=255: issues.append(f"{size}/{fc}/{clip}/{f} no pivot")
                    fn=f"mon_{size}_{fc}_{clip}_{f:02d}.png"; im.save(os.path.join(d,fn))
                    paths.append(f"frames/{size}/{fc}/{fn}"); frames.append(im)
                s=Image.new("RGBA",(W*n,H))
                for i,im in enumerate(frames): s.paste(im,(i*W,0))
                sp=os.path.join(HERE,"strips",size,fc); os.makedirs(sp,exist_ok=True)
                sn=f"mon_{size}_{fc}_{clip}_strip.png"; s.save(os.path.join(sp,sn))
                man["clips"].append({"size":size,"canvas":[W,H],"pivot":[W//2,H-1],"facing":fc,"clip":clip,
                    "frames":n,"fps":fps,"mode":mode,"mirrored_from":{"sw":"se","nw":"ne"}.get(fc),
                    "paths":paths,"strip":f"strips/{size}/{fc}/{sn}"})
                pv=os.path.join(HERE,"previews"); os.makedirs(pv,exist_ok=True)
                if (size=="medium" and fc=="se") or (clip=="idle" and fc=="se"):
                    k=4; s.resize((s.width*k,s.height*k),Image.NEAREST).save(os.path.join(pv,f"preview_{size}_{fc}_{clip}_x4.png"))
    man["engine"]={"missing_clip":"idle","death":"hold_last","aliases":{"climb":"walk","crawl":"walk","hide":"idle","melee":"attack"}}
    json.dump(man,open(os.path.join(HERE,"clips.json"),"w"),indent=1)
    print("clips",len(man["clips"]),"issues",issues)
if __name__=="__main__": main()
