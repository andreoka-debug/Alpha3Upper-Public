"""First-run setup and launcher. Game resources stay in the user's local cache."""
from pathlib import Path
import ctypes, json, os, queue, shutil, subprocess, sys, tempfile, threading, time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from import_game import capture_live, extract_assets, verify_import, write_manifest

VERSION='0.16.0'
def bundle_root():return Path(sys.executable).parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parents[1]/'dist'
def runtime_root():return Path(os.environ.get('LOCALAPPDATA',Path.home()/'AppData/Local'))/'Alpha3UpperPublic'/'runtime-v1'

class App:
    def __init__(self):
        self.root=tk.Tk();self.root.title('Alpha 3 Upper Public — configuração');self.root.geometry('720x440');self.root.minsize(680,410)
        self.events=queue.Queue();self.busy=False;self.cache=runtime_root();self.folder=tk.StringVar();self.status=tk.StringVar(value='Selecione sua instalação original para começar.')
        try:self.folder.set(json.loads((self.cache.parent/'launcher.json').read_text()).get('installation',''))
        except (OSError,ValueError):pass
        ttk.Style().theme_use('vista');frame=ttk.Frame(self.root,padding=24);frame.pack(fill='both',expand=True)
        ttk.Label(frame,text='ALPHA 3 UPPER',font=('Segoe UI',23,'bold')).pack(anchor='w')
        ttk.Label(frame,text='Public 0.16 • seus arquivos, importados no seu PC',font=('Segoe UI',11)).pack(anchor='w',pady=(0,18))
        ttk.Label(frame,text='Pasta do Capcom Fighting Collection 2 original').pack(anchor='w')
        row=ttk.Frame(frame);row.pack(fill='x',pady=7);self.entry=ttk.Entry(row,textvariable=self.folder);self.entry.pack(side='left',fill='x',expand=True)
        self.browse=ttk.Button(row,text='Escolher pasta…',command=self.choose);self.browse.pack(side='right',padx=(8,0))
        ttk.Label(frame,text='Na primeira importação, abra a coletânea normalmente pela Steam,\nentre no Alpha 3 Upper e deixe uma partida ou treino aberto.\nDepois, os recursos ficam preparados para as próximas execuções.',wraplength=640,justify='left').pack(anchor='w',pady=9)
        ttk.Label(frame,textvariable=self.status,wraplength=630,justify='left').pack(anchor='w',pady=12)
        self.progress=ttk.Progressbar(frame,mode='indeterminate');self.progress.pack(fill='x')
        buttons=ttk.Frame(frame);buttons.pack(fill='x',pady=18)
        self.import_button=ttk.Button(buttons,text='Importar da instalação',command=lambda:self.start(self.import_game));self.import_button.pack(side='left')
        self.play_button=ttk.Button(buttons,text='Jogar',command=lambda:self.start(self.play));self.play_button.pack(side='right')
        ttk.Label(frame,text='Nenhum dado do jogo é incluído neste pacote ou enviado pela importação.',foreground='#555').pack(anchor='w')
        self.root.protocol('WM_DELETE_WINDOW',self.close);self.root.after(100,self.poll)
        if (self.cache/'data/import.json').exists():self.status.set('Recursos preparados. Clique em Jogar; a integridade será conferida.')
    def choose(self):
        p=filedialog.askdirectory(title='Pasta da coletânea original')
        if p:self.folder.set(p)
    def notify(self,text):self.events.put(('status',text))
    def start(self,action):
        if self.busy:return
        self.selected=self.folder.get();self.busy=True;self.progress.start();self.toggle(False)
        def work():
            try:action()
            except Exception as e:self.events.put(('error',str(e)))
            finally:self.events.put(('done',None))
        threading.Thread(target=work,daemon=True).start()
    def toggle(self,on):
        for c in [self.entry,self.browse,self.import_button,self.play_button]:c.configure(state='normal' if on else 'disabled')
    def core(self):
        p=bundle_root()/'Alpha3Upper-Core.exe'
        if not p.is_file():raise RuntimeError('Alpha3Upper-Core.exe não encontrado ao lado do iniciador.')
        return p
    def import_game(self):
        self.notify('Conferindo a versão e lendo o código do original...')
        image=capture_live(self.selected);self.cache.parent.mkdir(parents=True,exist_ok=True)
        # Generated game data is outside the repository and release folder.
        with tempfile.TemporaryDirectory(prefix='import-',dir=self.cache.parent) as temp:
            stage=Path(temp);assert stage.resolve().is_relative_to(self.cache.parent.resolve())
            extract_assets(self.selected,stage/'data',self.notify);(stage/'data/main.bin').write_bytes(image);del image
            shutil.copy2(self.core(),stage/'Alpha3Upper-Core.exe')
            (stage/'data/settings.ini').write_text('[Audio]\nEfeitosVozes=50\nMusica=100\n[Partida]\nDificuldade=3\nRoundsParaVencer=3\n')
            self.notify('Testando os recursos importados...')
            p=subprocess.run([str(stage/'Alpha3Upper-Core.exe'),'--self-test','3500'],creationflags=subprocess.CREATE_NO_WINDOW,timeout=90)
            log=(stage/'data/run.log').read_text(errors='replace');error=(stage/'data/error.log').read_text(errors='replace')
            if p.returncode or error.strip() or 'frames=3500' not in log or 'missing_peak=0' not in log or 'failed=0 unmapped=0' not in log:raise RuntimeError('A validação local não passou. Os recursos anteriores foram preservados.\n'+error[-600:])
            # Retain only locally imported resources and user settings, not probes.
            for f in (stage/'data').iterdir():
                if f.is_file() and f.name not in ['main.bin','arena.bin','audio-map.tsv','settings.ini']:f.unlink()
            write_manifest(stage/'data');self.cache.mkdir(parents=True,exist_ok=True)
            previous=self.cache/('data.previous-'+str(time.time_ns()));current=self.cache/'data'
            if current.exists():
                if (current/'settings.ini').exists():shutil.copy2(current/'settings.ini',stage/'data/settings.ini')
                current.rename(previous)
            try:(stage/'data').rename(current)
            except Exception:
                if previous.exists():previous.rename(current)
                raise
            shutil.copy2(self.core(),self.cache/'Alpha3Upper-Core.exe')
            (self.cache.parent/'launcher.json').write_text(json.dumps({'installation':str(Path(self.selected).resolve())},ensure_ascii=False),encoding='utf-8')
        self.notify('Importação concluída e testada. Você já pode fechar o original e clicar em Jogar.')
    def play(self):
        self.notify('Verificando os recursos locais...')
        if not verify_import(self.cache/'data'):raise RuntimeError('Recursos ausentes ou alterados. Use Importar da instalação para preparar os arquivos originais localmente.')
        shutil.copy2(self.core(),self.cache/'Alpha3Upper-Core.exe')
        subprocess.Popen([str(self.cache/'Alpha3Upper-Core.exe')],cwd=self.cache,creationflags=subprocess.CREATE_NO_WINDOW)
        self.notify('Jogo iniciado. Online → Lobby e chat ou Criar / entrar em partida.')
    def poll(self):
        while not self.events.empty():
            kind,value=self.events.get()
            if kind=='status':self.status.set(value)
            elif kind=='error':self.status.set('Não foi possível concluir.');messagebox.showerror('Alpha3Upper Public',value)
            elif kind=='done':self.busy=False;self.progress.stop();self.toggle(True)
        self.root.after(100,self.poll)
    def close(self):
        if self.busy:messagebox.showinfo('Aguarde','Aguarde a importação terminar antes de fechar.');return
        self.root.destroy()

if __name__=='__main__':
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    if '--verify-local' in sys.argv:raise SystemExit(0 if verify_import(runtime_root()/'data') else 2)
    App().root.mainloop()
