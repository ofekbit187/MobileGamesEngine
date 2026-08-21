#!/usr/bin/env python3
"""SPIKE — can our own 17-joint rig be fitted directly to the multi-view 2D landmarks?

    ./build/tools/mocap/mge_gait_dump --speed 1.355 --out build/engine_walk.json   # for the rig
    python3 tools/mocap/rigfit_spike.py

**THIS IS A FEASIBILITY SPIKE, NOT AN IMPLEMENTATION, AND IT IS NOT TASK 18.2's ANSWER.**
It exists to turn a proposal into a measurement before the architect is asked to rule on it.
Nothing else imports it, nothing in the engine depends on it, and it should be deleted or
rewritten properly once there is a ruling — not extended in place.

THE QUESTION. 18.2 measured that combining per-view 3D point estimates does not beat the best
single view on any fold (`multiview.py`, and `docs/research/multiview-triangulation.md`).
Averaging point estimates cannot beat the best of them, and no weighting fixes a bias. What is
missing is a CONSTRAINT — and the obvious one is that these landmarks belong to a body whose
bones do not change length. We already have exactly that object: the rig.

So instead of triangulating points and later converting points to rotations, this fits the rig's
JOINT ROTATIONS directly so that the rig reprojects into every view at once. Bone lengths are
fixed by construction, knees and elbows are 1-DOF hinges because that is what they are, and the
filmed man's proportions are absorbed by a handful of scale factors fitted ONCE for the take —
which is our `HumanoidVariant`, not a per-frame fudge.

It is scored by the same held-out protocol as `multiview.py`: build from two views, fit a camera
for the third, measure reprojection into an image the fit never saw.

WHAT IT DOES NOT DO, so nobody mistakes it for the real thing: no temporal continuity, no foot
contact constraint, no joint limits, no `.mgeanim` output, no rig-hash handling, and the fitted
proportions are not yet stable across folds (see the doc). It is slow and it is not tested.
"""

import gzip, json, numpy as np, time
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation as Rot

JOINTS=['Hips','Spine','Chest','Neck','Head','UpperArmL','ForearmL','HandL','UpperArmR','ForearmR','HandR',
        'ThighL','ShinL','FootL','ThighR','ShinR','FootR']
PARENT=[-1,0,1,2,3,2,5,6,2,8,9,0,11,12,0,14,15]
JI={n:i for i,n in enumerate(JOINTS)}
BIND=np.array(json.load(open('build/engine_walk.json'))['bind'])
# landmark -> rig joint
LM={11:'UpperArmL',12:'UpperArmR',13:'ForearmL',14:'ForearmR',15:'HandL',16:'HandR',
    23:'ThighL',24:'ThighR',25:'ShinL',26:'ShinR',27:'FootL',28:'FootR'}
MP_IDX=np.array(list(LM)); JOINT_IDX=np.array([JI[LM[i]] for i in MP_IDX])
# free DOF: 3 for ball joints, 1 (hinge about X) for knees and elbows
FREE=[('Hips',3),('Chest',3),('ThighL',3),('ThighR',3),('ShinL',1),('ShinR',1),
      ('UpperArmL',3),('UpperArmR',3),('ForearmL',1),('ForearmR',1)]
NP_=sum(n for _,n in FREE)
# proportion scales applied to bind offsets, fitted ONCE for the take (this is our
# HumanoidVariant: the filmed man is not our template, and that is retargeting's job)
PROP=[('torso',[JI['Spine'],JI['Chest'],JI['Neck'],JI['Head']]),
      ('shoulder',[JI['UpperArmL'],JI['UpperArmR']]),
      ('upperarm',[JI['ForearmL'],JI['ForearmR']]),
      ('forearm',[JI['HandL'],JI['HandR']]),
      ('hip',[JI['ThighL'],JI['ThighR']]),
      ('thigh',[JI['ShinL'],JI['ShinR']]),
      ('shin',[JI['FootL'],JI['FootR']])]

def fk(params, bind):
    """Joint world positions for one frame. params packs FREE in order."""
    R=[np.eye(3)]*17; P=[np.zeros(3)]*17
    q={}; k=0
    for name,n in FREE:
        if n==3: q[JI[name]]=Rot.from_rotvec(params[k:k+3]).as_matrix(); k+=3
        else:    q[JI[name]]=Rot.from_rotvec([params[k],0,0]).as_matrix(); k+=1
    for j in range(17):
        Rj=q.get(j,np.eye(3)); p=PARENT[j]
        if p<0: P[j]=np.zeros(3); R[j]=Rj
        else:   P[j]=P[p]+R[p]@bind[j]; R[j]=R[p]@Rj
    return np.array(P)

def load(path,window):
    d=json.load(gzip.open(path,'rt')); out={}
    for v,vd in d['views'].items():
        a=np.array([r['img'] for r in vd['data']]); _,_,w,h=d['panels'][v]
        px=np.stack([a[:,:,0]*w,a[:,:,1]*h],-1); sl=slice(*window)
        out[v]={'image':px[sl],'vis':np.maximum(a[:,:,2][sl],1e-3),
                'world':np.array([r['world'] for r in vd['data']])[sl],
                'torso':float(np.median(np.linalg.norm(px[sl][:,[11,12]].mean(1)-px[sl][:,[23,24]].mean(1),axis=1)))}
    return out
WIN=(154,292); V=load('assets/mocap/walk_reference_tracking.json.gz',WIN)
NAMES=list(V); F=len(V[NAMES[0]]['image'])

def cam_project(X, Rv, U, vis):
    """Scaled-orthographic with per-frame scale+offset in closed form."""
    P=(Rv[:2]@X.T).T
    w=vis/vis.sum()
    Pm=(w[:,None]*P).sum(0); Um=(w[:,None]*U).sum(0)
    Pc=P-Pm; Uc=U-Um
    s=(w[:,None]*Pc*Uc).sum()/max((w[:,None]*Pc*Pc).sum(),1e-12)
    return s*Pc+Um, Uc+Um

def fit_frame(f, views, Rv, bind, x0):
    def res(p):
        X=fk(p,bind)[JOINT_IDX]
        out=[]
        for v in views:
            U=V[v]['image'][f][MP_IDX]; vis=V[v]['vis'][f][MP_IDX]
            Pp,Uu=cam_project(X,Rv[v],U,vis)
            out.append(((Pp-Uu)*vis[:,None]/V[v]['torso']).ravel())
        # weak prior toward zero rotation, so unobserved DOF stay sane
        out.append(0.02*np.asarray(p))
        return np.concatenate(out)
    r=least_squares(res,x0,method='lm',max_nfev=120)
    return r.x, float(np.sqrt(np.mean(r.fun[:-NP_]**2)))

def kabsch(P,Q,w):
    w=w/w.sum(); Pc=P-(w[:,None]*P).sum(0); Qc=Q-(w[:,None]*Q).sum(0)
    U,S,Vt=np.linalg.svd((Pc*w[:,None]).T@Qc)
    return Vt.T@np.diag([1,1,np.sign(np.linalg.det(Vt.T@U.T))])@U.T
def meanR(Rs):
    U,_,Vt=np.linalg.svd(np.mean(Rs,0)); return U@np.diag([1,1,np.sign(np.linalg.det(U@Vt))])@Vt
BODY=np.array([11,12,13,14,15,16,23,24,25,26,27,28,29,30,31,32])

def held_out_error(X3d, held):
    """Same protocol as multiview.py: fit a camera fresh, score reprojection."""
    U=V[held]['image'][:,MP_IDX]; vis=V[held]['vis'][:,MP_IDX]
    def res(p):
        R=Rot.from_rotvec(p).as_matrix(); out=[]
        for f in range(len(X3d)):
            Pp,Uu=cam_project(X3d[f],R,U[f],vis[f])
            out.append(((Pp-Uu)*vis[f][:,None]).ravel())
        return np.concatenate(out)
    best=np.inf
    for seed in range(8):
        p0=np.zeros(3) if seed==0 else Rot.random(random_state=seed).as_rotvec()
        r=least_squares(res,p0,method='lm',max_nfev=300)
        best=min(best,float(np.sqrt(np.mean(r.fun**2))))
    return best/V[held]['torso']*100

t0=time.time()
print("SPIKE: fit the 17-joint rig to 2 views, score on the third (same protocol as 18.2)")
print(f"  {NP_} rotation DOF per frame ({len(FREE)} joints; knees and elbows are 1-DOF hinges),")
print(f"  {len(MP_IDX)} landmarks x 2 views = {len(MP_IDX)*4} observations. Bone lengths FIXED.\n")
for held in NAMES:
    others=[v for v in NAMES if v!=held]
    ref=others[0]
    Rv={ref:np.eye(3)}
    for v in others[1:]:
        Rv[v]=meanR(np.array([kabsch(V[v]['world'][f][BODY]-V[v]['world'][f][[23,24]].mean(0),
                                     V[ref]['world'][f][BODY]-V[ref]['world'][f][[23,24]].mean(0),
                                     np.minimum(V[v]['vis'][f][BODY],V[ref]['vis'][f][BODY])) for f in range(F)]))
    # proportion scales fitted once on a sample of frames, then poses on every frame
    sample=list(range(0,F,14))
    def with_props(sc):
        b=BIND.copy()
        for k,(nm,js) in enumerate(PROP):
            for j in js: b[j]=BIND[j]*sc[k]
        return b
    def prop_res(sc):
        b=with_props(sc); e=[]; x=np.zeros(NP_)
        for f in sample:
            x,err=fit_frame(f,others,Rv,b,x); e.append(err)
        return np.array(e)
    sc=least_squares(prop_res,np.ones(len(PROP)),method='lm',max_nfev=12,diff_step=0.08).x
    bind=with_props(sc)
    X=np.zeros((F,len(MP_IDX),3)); x=np.zeros(NP_); errs=[]
    for f in range(F):
        x,err=fit_frame(f,others,Rv,bind,x); X[f]=fk(x,bind)[JOINT_IDX]; errs.append(err)
    print(f"{held:14s} train fit {np.mean(errs)*100:5.1f}%   HELD-OUT {held_out_error(X,held):5.1f}% of torso"
          f"   proportions {', '.join(f'{n}:{s:.2f}' for (n,_),s in zip(PROP,sc))}")
print(f"\n({time.time()-t0:.0f}s)")
