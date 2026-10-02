#!/usr/bin/env python3
"""Regenerate the checked-in manifest, parameter list and native MPC layout."""
import json
from pathlib import Path
D=Path(__file__).resolve().parent
P=[]; L=['style=dark','theme_bg=171b24','theme_accent=4bcbd6']
def add(k,n,**kw):
    P.append(dict(key=k,name=n,**kw));return k
roots=['C','Db','D','Eb','E','F','Gb','G','Ab','A','Bb','B']
scales=['Major','Minor','Dorian','Mixolyd','Maj Pent','Min Pent','Chromatic']
fields=[('enabled','Run',dict(options=['Off','On'],default=1)),('speed','Speed',dict(options=['1/4x','1/2x','3/4x','1x','3/2x','2x','3x','4x','4:3','5:4','7:4','2:3','4:5','4:7'],default=3)),('direction','Direction',dict(options=['Forward','Reverse','Ping Pong'])),('channel','Output Ch',dict(min=1,max=16,default=1,display='int')),('shift','Semitones',dict(min=-48,max=48,default=0,display='int')),('source','Source Key',dict(options=roots)),('target','Target Key',dict(options=roots)),('source_scale','Source Scale',dict(options=scales)),('target_scale','Target Scale',dict(options=scales)),('transform','Mapping',dict(options=['Transpose','Fit Scale','Degree Map'])),('follow','Input Root',dict(options=['Off','On'])),('anchor','Anchor Note',dict(min=0,max=127,default=60,display='int')),('input','Input Ch',dict(min=0,max=16,default=0,display='int')),('latch','Latch Root',dict(options=['Off','On'],default=1)),('loop','Loop',dict(options=['One Shot','Loop'],default=1))]
for t in range(4):
    keys=[]
    for key,name,kw in fields:
        kw=kw.copy()
        if key=='channel':kw['default']=t+1
        keys.append(add(f't{t}_{key}',name,**kw))
    L += [f'[tab Track {chr(65+t)}]',f'frame x=20 y=96 w=1240 h=605 title="Track {chr(65+t)} - MIDI Clip"']
    for i,key in enumerate(keys):
        x=155+(i%5)*240;y=210+(i//5)*185
        L.append(f'knob cx={x} cy={y} r=34 label="{fields[i][1]}" key={key}')
    L.append(f'qlinks "Track {chr(65+t)}" = '+','.join(keys))
for k,n,kw in [('selected','File Index',dict(min=0,max=4095,display='int')),('destination','Load Track',dict(options=['A','B','C','D'])),('part','File Track',dict(min=0,max=64,display='int')),('file_channel','File Ch',dict(min=0,max=16,display='int'))]:add(k,n,**kw)
add('chord_step','Chord Beats',options=['1/16','1/8','1/4','1/2','1','2','4','8'],default=4)
for k,n in [('up','Up'),('refresh','Refresh'),('open','Open Folder'),('load','Load'),('preview','Preview'),('stop_preview','End Preview'),('panic','Panic')]:add(k,n,min=0,max=1,momentary=True)
for k,n in [('folder','Folder'),('status','Status'),('output','MIDI Output')]+[(f'row{i}',f'File {i+1}')for i in range(8)]+[(f'clip{i}',f'Track {chr(65+i)}')for i in range(4)]:add(k,n,min=0,max=1,display='string',type='readout')
L+=['[tab Browser]','readout cx=640 cy=128 w=1200 h=36 label="Folder" key=folder']
for i in range(8):L.append(f'readout cx={330+(i//4)*620} cy={200+(i%4)*63} w=590 h=38 label="File {i+1}" key=row{i}')
for i,k in enumerate(['selected','destination','part','file_channel']):L.append(f'knob cx={170+i*305} cy=535 r=32 label="{["File Index","Load Track","File Track (0=All)","File Ch (0=All)"][i]}" key={k}')
for i,k in enumerate(['up','refresh','open','load','preview','stop_preview']):L.append(f'button cx={110+i*210} cy=652 label="{["Up","Refresh","Folder","Load","Preview","End Preview"][i]}" key={k}')
L+=['qlinks "Browse" = selected,destination,part,file_channel','[tab Status]','readout cx=640 cy=155 w=1180 h=48 label="Status" key=status','readout cx=640 cy=250 w=1180 h=48 label="Output" key=output']
for i in range(4):L.append(f'readout cx=640 cy={335+i*72} w=1180 h=44 label="Track {chr(65+i)}" key=clip{i}')
L+=['knob cx=260 cy=608 r=28 label="Chord Beats on Load" key=chord_step','button cx=900 cy=640 label="Panic" key=panic','qlinks "Status" = chord_step,panic']
(D/'params.json').write_text(json.dumps({'name':'QUADWEAVE','params':P},indent=2)+'\n')
(D/'layout.conf').write_text('\n'.join(L)+'\n')
(D/'vst.json').write_text(json.dumps({'name':'QUADWEAVE','vendor':'MPC-MOD','uid':'QdW1','version':100,'so':'quadweave.so','params':'params.json','layout':'layout.conf','build':{'root':'.','sources':['src/midi.cpp','src/progression.cpp','src/player.cpp','src/plugin.cpp'],'cflags':['-std=c++17'],'libs':['-lasound','-lpthread','-ldl']}},indent=2)+'\n')
