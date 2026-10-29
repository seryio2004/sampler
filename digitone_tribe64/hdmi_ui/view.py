"""Passive HDMI display: restrained editorial layout and audio telemetry."""
from collections import deque
import math

BG = '#abbab9'
PANEL = '#b7c4c2'
PAPER = '#fefefe'
EDGE = '#778b88'
TEXT = '#1e1e1e'
DIM = '#465c59'
ACCENT = '#00b7a5'
MUTED = '#829794'
MODES = ['SEQ', 'FX', 'BPM', 'BASS', 'DRUM', 'FILES', 'SONG', 'LIVE', 'BANK', 'MUTE', 'SOUND']
LABELS = ['Patrón', 'Efectos', 'Tempo', 'Bajo', 'Pads', 'Samples', 'Canción', 'Lanzar', 'Escenas', 'Mute', 'Sonido']
FX = ['OFF', 'LOWPASS', 'HIGHPASS', 'BITCRUSH', 'DIST', 'ECHO', 'STUTTER', 'TREMOLO', 'RING MOD', 'CHORUS', 'FLANGER']
NAMES = ['BOMBO', 'CAJA', 'HAT CER', 'HAT ABI', 'PALMAS', 'TOM', 'FM TRIBAL', 'FM METAL', 'FM BASS']


def demo_state():
    from pathlib import Path
    import json
    scenes = json.loads((Path(__file__).resolve().parents[1]/'kits/tribe64.json').read_text())['scenes']
    signals = [[int(9000*math.sin(i*(.09+t*.037)+t)*(.55+.45*math.cos(i*.013+t))) for i in range(128)] for t in range(10)]
    return dict(protocol=1, demo=True, mode='SEQ', bpm=145, playing=1, controller=1, fn=0,
                edit_scene=0, active_scene=0, queued_scene=-1, live_selected=0, cursor=6, playhead=6,
                drum=0, bass=0, octave=2, voices=517, solo_track=-1, mute_scene=0, mute_return=0,
                stop_pending=0, volume=.68, pattern=scenes[0]['patterns'], melody=scenes[0]['bass_pattern'],
                tracks=[dict(name=n, pitch=0., cutoff=1., length=1.) for n in NAMES], muted=[0]*10, stored_muted=[0]*10,
                scenes=[dict(name=s['name'], notes=sum(sum(r) for r in s['patterns'])) for s in scenes],
                song=[[0, 4], [1, 2], [0, 4], [2, 1]], song_cursor=0, song_choice=0, song_times=4,
                song_position=0, song_left=3, song_loop=1, song_pending=0, song_action=0,
                bank_action=0, bank_target=0, name_edit=0, name_cursor=0, name_draft='', fx_slot=0,
                fx=[dict(effect=0, enabled=0, pending=-1, a=.65, b=.2), dict(effect=5, enabled=1, pending=-1, a=.4, b=.3)],
                scope=[int(14000*math.sin(i*.16)*(.7+.3*math.sin(i*.04))) for i in range(256)],
                track_scopes=signals, audio_frame=44100,
                waveform=[[-int(28000*math.exp(-i/25)*abs(math.sin(i*.8))),
                           int(28000*math.exp(-i/25)*abs(math.cos(i*.7)))] for i in range(128)],
                sample_frames=21168, sample_position=4700, waveform_preview=0, waveform_name='',
                files=[dict(index=i, name=n+'.wav', directory=0) for i, n in enumerate(NAMES)],
                file_selected=0, file_count=9, directory='samples/', loading=0, preview_ready=0,
                menu_open=0, menu_selected=0, menu=[], message='')


class View:
    """Rendering only. The sampler remains the sole source of state and input."""
    def __init__(self):
        self.history = deque(maxlen=96)
        self.last_frame = None

    def label(self, x, y, text, color=DIM, anchor='nw'):
        self.p.text(x, y, text, 10, color, anchor=anchor, family='mono')

    def rule(self, x, y, width):
        self.p.line([x, y, x+width, y], EDGE)

    def trace(self, x, y, w, h, samples, color=TEXT):
        self.p.line([x, y+h/2, x+w, y+h/2], EDGE)
        if len(samples)<2:
            return
        points=[]
        for i,sample in enumerate(samples):
            points.extend((x+i*w/(len(samples)-1), y+h/2-max(-32768,min(32767,sample))/32768*h*.46))
        self.p.line(points,color,1.5)

    def source(self, x, y, w, h):
        s=self.s
        trim=s['tracks'][min(8,s['drum'])]['length'] if s['mode']!='BASS' and s['drum']<9 else 1
        if trim<1:
            self.p.rect(x+w*trim,y,x+w,y+h,PANEL)
        self.p.line([x,y+h/2,x+w,y+h/2],EDGE)
        for i,(lo,hi) in enumerate(s['waveform']):
            xx=x+(i+.5)*w/128
            self.p.line([xx,y+h/2-lo/32768*h*.46,xx,y+h/2-hi/32768*h*.46],TEXT if i/128<trim else MUTED,2)
        if s['sample_position']:
            fraction=min(1,s['sample_position']/max(1,s['sample_frames']))
            self.p.line([x+w*fraction,y,x+w*fraction,y+h],ACCENT,2)

    def readout(self, x, y, width, title, value, fraction=None):
        self.label(x,y,title)
        self.p.text(x,y+24,value,27,TEXT)
        if fraction is not None:
            self.p.rect(x,y+64,x+width,y+66,EDGE)
            self.p.rect(x,y+64,x+width*min(1,max(0,fraction)),y+66,TEXT)

    def draw(self, painter, state, connected=True):
        self.p,self.s=painter,state
        p,s=painter,state
        p.rect(0,0,1280,800,BG)
        p.text(40,24,'Tribe64',17,TEXT,bold=True)
        self.label(1240,28,'DEMO' if s.get('demo') else '●' if connected else 'Sin señal',anchor='ne')
        p.text(40,57,LABELS[MODES.index(s['mode'])],48,TEXT)
        scene=s['mute_scene'] if s['mode']=='MUTE' else s['edit_scene'] if s['mode'] in ('SEQ','BASS','BANK') or not s['playing'] else s['active_scene']
        self.label(480,67,f"Edita E{scene+1:02d} · Suena E{s['active_scene']+1:02d}" if s['playing'] and scene!=s['active_scene'] else f'E{scene+1:02d}')
        p.text(480,88,s['scenes'][scene]['name'][:24],15,TEXT)
        self.label(832,67,'Paso')
        p.text(832,87,f"{s['playhead']+1:02d} / 64",18,TEXT,family='mono')
        self.label(1020,67,'BPM')
        p.text(1020,86,str(s['bpm']),25,TEXT)
        p.text(1240,92,'Parando' if s.get('stop_pending') else 'PLAY' if s['playing'] else 'STOP',12,TEXT,anchor='ne',family='mono')
        self.rule(40,130,1200)
        mode=s['mode']
        if mode in ('SEQ','BASS'):
            self.pattern()
        elif mode=='MUTE':
            self.mutes()
        elif mode in ('SOUND','DRUM'):
            self.sound()
        elif mode=='SONG':
            self.song()
        elif mode in ('LIVE','BANK'):
            self.scenes()
        elif mode=='FILES':
            self.files()
        elif mode=='FX':
            self.effects()
        else:
            self.tempo()
        p.line([924,152,924,744],EDGE)
        self.sidebar()
        self.scopes()
        message=s.get('message','')
        if message and not message.startswith(('ARDUINO','OFF:')):
            self.label(40,772,message[:90])
        if s.get('menu_open'):
            self.menu()
        if not connected and not s.get('demo'):
            p.rect(312,306,968,398,PAPER,TEXT)
            p.text(640,352,'Sin señal del sampler',22,TEXT,anchor='center')

    def pattern(self):
        p,s=self.p,self.s
        page=s['cursor']//16
        self.label(40,163,'64 pasos')
        for i in range(4):
            x=432+i*118
            if i==page:
                p.rect(x,152,x+106,183,TEXT)
            p.text(x+53,168,f'{16*i+1:02d}–{16*i+16:02d}',11,PAPER if i==page else DIM,anchor='center',family='mono')
        gx,cw,gy,rh=196,43.75,219,32
        for i in range(16):
            self.label(gx+(i+.5)*cw-3,200,f'{page*16+i+1:02d}',TEXT if i%4==0 else DIM,anchor='center')
        for row in range(10):
            y=gy+row*rh
            selected=row==s['drum'] if row<9 else s['mode']=='BASS'
            if selected:
                p.rect(40,y,904,y+rh-3,PANEL)
                p.rect(40,y,43,y+rh-3,TEXT)
            name=s['tracks'][row]['name'] if row<9 else 'BAJO'
            self.label(51,y+9,f'{row+1:02d}',TEXT)
            p.text(78,y+8,name[:14],11,TEXT)
            for col in range(16):
                step=page*16+col
                active=s['pattern'][row][step] if row<9 else s['melody'][step]>=0
                color=MUTED if s['muted'][row] else ACCENT if selected else TEXT
                x=gx+col*cw
                p.rect(x,y+3,x+cw-5,y+rh-6,color if active else PANEL,EDGE)
                if active and row==9:
                    p.text(x+(cw-5)/2,y+rh/2-2,str(s['melody'][step]+1),10,TEXT if s['muted'][row] else PAPER,anchor='center',family='mono')
                if s['playing'] and s['playhead']==step:
                    p.rect(x,y+3,x+cw-5,y+rh-6,color if active else BG,ACCENT)
                if selected and s['cursor']==step:
                    p.line([x,y+rh-3,x+cw-5,y+rh-3],TEXT,2)
        self.label(40,562,'01')
        self.label(904,562,'64',anchor='ne')
        for step in range(64):
            count=sum(bool(row[step]) for row in s['pattern'])+(s['melody'][step]>=0)
            x=40+step*13.5
            p.rect(x,592-count*2,x+10,601,ACCENT if s['playing'] and s['playhead']==step else TEXT if step//16==page else MUTED)

    def sidebar(self):
        p,s=self.p,self.s
        if s['mode'] in ('SOUND','DRUM'):
            self.label(944,157,'Pistas')
            for i in range(10):
                y=181+i*41
                selected=i==s['drum']
                color=MUTED if s['muted'][i] else ACCENT if selected else TEXT
                self.label(944,y,f"{i+1:02d}  "+(s['tracks'][i]['name'] if i<9 else 'BAJO')[:22],DIM if s['muted'][i] else TEXT)
                self.trace(944,y+14,296,24,s['track_scopes'][i],color)
            return
        pad=s['drum'] if s['drum']<9 and s['mode']!='BASS' else 9
        name=s['tracks'][pad]['name'] if pad<9 else 'BAJO'
        self.label(944,157,'Sample' if not s.get('waveform_preview') else 'Preview')
        p.text(944,179,(s.get('waveform_name') or name)[:24],16,TEXT)
        self.source(944,214,296,65)
        if s['mode']=='SONG':
            self.label(944,299,'Bloque')
            p.text(944,319,f"{s['song_cursor']+1:02d}   E{s['song_choice']+1:02d}   ×{s['song_times']:02d}",15,TEXT,family='mono')
        elif pad==9:
            self.label(944,299,'Nota / Octava')
            p.text(944,319,f"{['C','D','E','G','A','C+'][s['bass']]} / {s['octave']}",15,TEXT,family='mono')
        else:
            t=s['tracks'][pad]
            for x,label,value in [(944,'Tono',f"{t['pitch']:+.1f}"),(1046,'Filtro',f"{t['cutoff']*100:.0f}%"),(1148,'Long.',f"{t['length']*100:.0f}%")]:
                self.label(x,299,label)
                p.text(x,319,value,15,TEXT,family='mono')
        self.rule(944,350,296)
        if s['mode']=='MUTE':
            self.readout(944,377,296,'Activas',f"{10-sum(s['muted']):02d} / 10")
            self.readout(944,482,296,'Solo' if s['solo_track']>=0 else 'Pista',f"{s['solo_track']+1:02d}" if s['solo_track']>=0 else f"{s['drum']+1:02d}")
            return
        for i in range(10):
            y=365+i*23
            selected=i==pad
            color=ACCENT if selected and not s['muted'][i] else MUTED if s['muted'][i] else TEXT
            self.label(944,y+5,f'{i+1:02d}',DIM if s['muted'][i] else TEXT)
            self.label(970,y+5,(s['tracks'][i]['name'] if i<9 else 'BAJO')[:8],DIM if s['muted'][i] else TEXT)
            self.trace(1058,y,182,21,s['track_scopes'][i],color)

    def scopes(self):
        p,s=self.p,self.s
        self.rule(40,618,864)
        self.label(40,634,'Mezcla')
        if s['mode'] in ('SOUND','DRUM'):
            self.trace(40,661,864,77,s['scope'])
        else:
            self.trace(40,661,552,77,s['scope'])
            track=s['drum'] if s['drum']<9 and s['mode']!='BASS' else 9
            self.label(624,634,f'Pista {track+1:02d}')
            self.trace(624,661,280,77,s['track_scopes'][track],ACCENT)
        self.rule(944,618,296)
        self.label(944,634,'Nivel')
        peak=max((abs(v)/32768 for v in s['scope']),default=0)
        rms=math.sqrt(sum((v/32768)**2 for v in s['scope'])/max(1,len(s['scope'])))
        frame=s.get('audio_frame',0)
        if frame!=self.last_frame:
            self.history.append(peak)
            self.last_frame=frame
        p.rect(944,660,1240,664,MUTED)
        p.rect(944,660,944+296*min(1,peak),664,TEXT)
        if len(self.history)>1:
            points=[]
            for i,v in enumerate(self.history):
                points.extend((944+i*296/(len(self.history)-1),705-min(1,v)*27))
            p.line(points,TEXT)
        self.label(944,726,f'{20*math.log10(max(1e-5,rms)):.1f} dB')
        self.label(1240,726,f"Vol. {s['volume']*100:.0f}%",anchor='ne')

    def mutes(self):
        p,s=self.p,self.s
        for i in range(9):
            x,y=40+i%3*292,153+i//3*132
            solo=s['solo_track']==i
            muted=s['muted'][i]
            p.rect(x,y,x+280,y+119,PANEL if muted else BG,TEXT)
            self.label(x+14,y+14,f'{i+1:02d}')
            self.label(x+266,y+14,'SOLO' if solo else 'OFF' if muted else 'ON',TEXT,anchor='ne')
            if solo:
                p.rect(x,y,x+4,y+119,ACCENT)
            p.text(x+14,y+39,s['tracks'][i]['name'][:19],17,DIM if muted else TEXT)
            self.trace(x+14,y+74,252,29,s['track_scopes'][i],MUTED if muted else ACCENT if solo else TEXT)
        self.rule(40,560,864)
        p.text(40,577,'10  Bajo',13,TEXT)
        self.label(200,581,'OFF' if s['muted'][9] else 'ON')
        self.trace(274,570,630,31,s['track_scopes'][9],MUTED if s['muted'][9] else TEXT)

    def sound(self):
        p,s=self.p,self.s
        pad=min(8,s['drum']);track=s['tracks'][pad]
        p.text(40,157,track['name'][:38],21,TEXT)
        self.label(904,166,f"{s['sample_frames']/44100:.3f} s",anchor='ne')
        self.source(40,200,864,190)
        self.rule(40,415,864)
        self.readout(40,439,250,'Tono',f"{track['pitch']:+.1f} st",(track['pitch']+24)/48)
        self.readout(332,439,250,'Filtro',f"{track['cutoff']*100:.0f}%",track['cutoff'])
        self.readout(624,439,280,'Longitud',f"{track['length']*100:.0f}%",track['length'])
        self.trace(40,536,864,65,s['track_scopes'][pad],TEXT)

    def song(self):
        p,s=self.p,self.s
        self.label(40,159,'Bloque')
        self.label(160,159,'Patrón')
        self.label(650,159,'Vueltas')
        self.rule(40,185,864)
        start=max(0,s['song_cursor']-5)
        for row,(scene,repeats) in enumerate(s['song'][start:start+11]):
            index=start+row;y=195+row*30
            selected=index==s['song_cursor']
            playing=s['playing'] and index==s['song_position']
            if selected:
                p.rect(40,y,904,y+28,PAPER)
            if playing:
                p.rect(40,y,44,y+28,ACCENT)
            self.label(54,y+8,f'{index+1:02d}',TEXT)
            p.text(160,y+6,f"E{scene+1:02d}  {s['scenes'][scene]['name'][:30]}",13,TEXT)
            self.label(650,y+8,f'×{repeats:02d}',TEXT)
            self.label(890,y+8,'PLAY' if playing else '',TEXT,anchor='ne')
        if not s['song']:
            p.text(40,214,'Sin bloques',16,DIM)
        self.rule(40,555,864)
        action=['Editar','Insertar','Borrar','Bucle'][s.get('song_action',0)]
        self.label(40,576,action)
        self.label(332,576,'Bucle '+('ON' if s['song_loop'] else 'OFF'))
        self.label(904,576,'Pendiente' if s.get('song_pending') else f"Restan {s['song_left']:02d}",anchor='ne')

    def scenes(self):
        p,s=self.p,self.s
        live=s['mode']=='LIVE'
        for i,scene in enumerate(s['scenes']):
            x,y=40+i%3*292,153+i//3*132
            active=i==s['active_scene'];queued=i==s['queued_scene']
            selected=i==s.get('live_selected',0) if live else i==s['edit_scene']
            p.rect(x,y,x+280,y+119,PAPER if selected else BG,TEXT)
            if queued or active:
                p.rect(x,y,x+4,y+119,ACCENT if queued else TEXT)
            self.label(x+14,y+14,f'E{i+1:02d}')
            self.label(x+266,y+14,'Siguiente' if queued else 'Suena' if active else '',TEXT,anchor='ne')
            p.text(x+14,y+43,scene['name'][:19],17,TEXT)
            self.label(x+14,y+91,f"{scene['notes']} notas")
        if not live:
            self.rule(40,560,864)
            action=['Editar','Copiar','Vaciar','Nombre','Lanzar'][s.get('bank_action',0)]
            self.label(40,578,action)
            if s.get('bank_action')==1:
                self.label(904,578,f"E{s.get('bank_target',0)+1:02d}",anchor='ne')
        if s.get('name_edit'):
            p.rect(100,326,844,426,PAPER,TEXT)
            self.label(120,342,'Nombre')
            p.text(120,371,s.get('name_draft','')[:35],25,TEXT)

    def files(self):
        p,s=self.p,self.s
        directory='/'+ '/'.join(s['directory'].rstrip('/').split('/')[-2:])
        p.text(40,157,directory[-74:],14,TEXT)
        self.label(904,161,f"P{s['drum']+1:02d}",anchor='ne')
        self.rule(40,185,864)
        entries=s['files'];current=next((i for i,e in enumerate(entries) if e['index']==s['file_selected']),0)
        start=max(0,current-5)
        for row,entry in enumerate(entries[start:start+11]):
            y=195+row*30;selected=entry['index']==s['file_selected']
            if selected:
                p.rect(40,y,904,y+28,PAPER)
                p.rect(40,y,44,y+28,TEXT)
            p.text(54,y+6,entry['name'][:82]+('/' if entry['directory'] else ''),13,TEXT)
        if not entries:
            p.text(40,214,'Sin archivos',16,DIM)
        if s.get('loading'):
            self.label(40,578,'Cargando…')

    def effects(self):
        p,s=self.p,self.s
        for slot in range(2):
            x=40+slot*444;effect=s['fx'][slot];pending=effect.get('pending',-1)
            self.label(x,159,f'FX {slot+1}')
            name=FX[effect['effect']] if effect['enabled'] else 'OFF'
            p.text(x,183,name,27,TEXT)
            if slot==s.get('fx_slot',0):
                p.line([x,224,x+420,224],TEXT,2)
            else:
                self.rule(x,224,420)
            for i,name in enumerate(FX):
                yy=246+(i//3)*38;xx=x+(i%3)*140
                selected=effect['enabled'] and effect['effect']==i
                if selected:
                    p.rect(xx,yy-4,xx+130,yy+24,ACCENT)
                p.text(xx+6,yy+2,name,11,TEXT,family='mono')
            self.readout(x,424,194,'A',f"{effect['a']*100:.0f}%",effect['a'])
            self.readout(x+222,424,198,'B',f"{effect['b']*100:.0f}%",effect['b'])
            if pending>=0:
                self.label(x,560,'Pendiente')
                p.text(x,579,FX[pending],15,TEXT)

    def tempo(self):
        p,s=self.p,self.s
        p.text(40,171,str(s['bpm']),146,TEXT)
        self.label(395,283,'BPM',TEXT)
        self.rule(40,369,864)
        self.readout(40,404,864,'Volumen',f"{s['volume']*100:.0f}%",s['volume'])
        p.text(40,540,'PLAY' if s['playing'] else 'STOP',24,TEXT)
