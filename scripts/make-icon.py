import sys
from PIL import Image, ImageDraw, ImageFilter
SS=8
def render(size):
    S=size*SS; im=Image.new('RGBA',(S,S),(0,0,0,0))
    # gradient rounded square
    grad=Image.new('RGBA',(S,S))
    top=(124,92,255); bot=(72,46,196)
    px=grad.load()
    for y in range(S):
        t=y/(S-1)
        for x in range(S):
            u=(x/(S-1))*0.35
            k=min(1,t*0.8+u)
            px[x,y]=tuple(int(top[i]+(bot[i]-top[i])*k) for i in range(3))+(255,)
    mask=Image.new('L',(S,S),0)
    pad=0 if size<=24 else int(S*0.03)
    r=int(S*(0.24 if size>24 else 0.22))
    ImageDraw.Draw(mask).rounded_rectangle([pad,pad,S-1-pad,S-1-pad],radius=r,fill=255)
    im.paste(grad,(0,0),mask)
    d=ImageDraw.Draw(im)
    # viewfinder brackets
    m=S*0.22; L=S*0.17; w=max(SS*1.6, S*(0.075 if size<=24 else 0.06))
    white=(255,255,255,245)
    for cx,cy,sx,sy in [(m,m,1,1),(S-m,m,-1,1),(m,S-m,1,-1),(S-m,S-m,-1,-1)]:
        d.rounded_rectangle([min(cx,cx+sx*L),min(cy,cy+sy*w)-0 if sy>0 else cy-w, max(cx,cx+sx*L), cy+w if sy>0 else cy],radius=w/2,fill=white)
        d.rounded_rectangle([cx if sx>0 else cx-w, min(cy,cy+sy*L), cx+w if sx>0 else cx, max(cy,cy+sy*L)],radius=w/2,fill=white)
    # glowing record dot
    R=S*(0.15 if size>24 else 0.17); c=S/2
    if size>=32:
        glow=Image.new('RGBA',(S,S),(0,0,0,0))
        ImageDraw.Draw(glow).ellipse([c-R*1.6,c-R*1.6,c+R*1.6,c+R*1.6],fill=(255,90,110,55))
        glow=glow.filter(ImageFilter.GaussianBlur(S*0.04))
        im=Image.alpha_composite(im,glow); d=ImageDraw.Draw(im)
    d.ellipse([c-R,c-R,c+R,c+R],fill=(255,77,94,255))
    if False:
        d.ellipse([c-R*0.55,c-R*0.7,c+R*0.05,c-R*0.15],fill=(255,255,255,60))
    return im.resize((size,size),Image.LANCZOS)
sizes=[16,20,24,32,40,48,64,128,256]
imgs=[render(s) for s in sizes]
imgs[-1].save(sys.argv[1],format='ICO',sizes=[(s,s) for s in sizes],append_images=imgs[:-1])
prev=Image.new('RGBA',(sum(sizes)+10*len(sizes),256),(240,240,240,255))
x=0
for s,i in zip(sizes,imgs): prev.alpha_composite(i,(x,0)); x+=s+10
prev.save(sys.argv[2])
