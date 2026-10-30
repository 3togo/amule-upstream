import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
from pathlib import Path
out=Path(__file__).parent
fig=plt.figure(figsize=(14,9),dpi=160,facecolor='#ffffff')
ax=fig.add_axes([0,0,1,1]);ax.set_xlim(0,1400);ax.set_ylim(900,0);ax.axis('off')
ink='#172b40';muted='#506275';blue='#1859a3';green='#17623f';red='#a03536'
def t(x,y,s,size=16,color=ink,weight='normal',**kw):
 ax.text(x,y,s,fontsize=size,color=color,fontweight=weight,va='top',fontfamily='DejaVu Sans',**kw)
def box(x,y,w,h,fill):
 ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0,rounding_size=12',facecolor=fill,edgecolor='none'))
t(55,35,'Flatpak: approve access, then choose what to share',25,weight='bold')
t(55,83,'Intended policy in PR #2017 · illustrative example, not a test result',13,color=muted)
t(55,132,'Example: a Music folder outside the app profile, with no download write grant',16,weight='bold')
cols=[365,700,1035]
headers=[('1  No approval','No folder grant'),('2  Approve folder…','Confirm Music in the system chooser'),('3  Share Music','Check Music, then press OK')]
for x,(a,b) in zip(cols,headers):
 box(x-5,180,310,104,'#edf4fc');t(x+12,198,a,17,color=blue,weight='bold');t(x+12,238,b,10.5,color=muted)
for x in [682,1017]:
 ax.annotate('',xy=(x+8,222),xytext=(x-8,222),arrowprops={'arrowstyle':'->','color':blue,'lw':2})
rows=[('Music visible in\nthe folder tree',['No','Yes','Yes']),('aMule can read\nMusic files',['No','Yes','Yes']),('Music selected\nfor sharing',['No','No — unchecked','Yes — checked']),('aMule can modify\nMusic files',['No','No','No'])]
for i,(label,values) in enumerate(rows):
 y=310+i*79
 if i%2==0: box(55,y-5,1290,73,'#f3f6f8')
 t(73,y+5,label,15,weight='bold')
 for x,v in zip(cols,values):
  t(x+12,y+13,v,17,color=green if v.startswith('Yes') else red,weight='bold')
box(55,650,1290,144,'#eaf4ee')
t(76,670,'Where writes ARE allowed',19,color=green,weight='bold')
t(76,710,'App profile and private app storage · Incoming / Temp · explicitly approved download destinations',14)
t(76,747,'Use a download Browse button for a write grant; a new write grant requires restarting aMule.',13,color=muted)
t(55,817,'Scope: standard manifest, no broad overrides. Profile/download write grants take precedence over sharing-only access.',11,color=muted)
t(55,847,'Private temporary/device areas remain available. Native packages keep normal filesystem permissions.',11,color=muted)
fig.savefig(out/'folder-policy.png',dpi=160,facecolor=fig.get_facecolor())
fig.savefig(out/'folder-policy.svg',facecolor=fig.get_facecolor())
