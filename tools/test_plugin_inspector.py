import importlib.util
import subprocess
import tempfile
from pathlib import Path
from types import SimpleNamespace
spec=importlib.util.spec_from_file_location('inspector',str(Path(__file__).with_name('inspect_plugin.py')))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
args=SimpleNamespace(glibc='2.39')
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 for symbol,fmt in [('VSTPluginMain','VST2'),('GetPluginFactory','VST3'),('lv2_descriptor','LV2'),('ladspa_descriptor','LADSPA'),('clap_entry','CLAP')]:
  (p/'fixture.c').write_text(f'void *{symbol}(void) {{ return 0; }}')
  subprocess.run(['gcc','-shared','-fPIC',str(p/'fixture.c'),'-o',str(p/'fixture.so')],check=True)
  r=m.inspect(p/'fixture.so',args,set())
  assert fmt in r['detected_formats'], r
  assert r['verdict']=='REBUILD_OR_ADAPT_REQUIRED',r
 (p/'bad.so').write_text('not ELF')
 assert m.inspect(p/'bad.so',args,None)['verdict']=='INSPECTION_ERROR'
 # Validate ARM verdict logic with representative GNU readelf output, no device claims.
 original=m.readelf
 def fake(path,*flags):
  return {'-hW':'Class: ELF32\nData: 2\'s complement, little endian\nType: DYN (Shared object file)\nMachine: ARM\nFlags: 0x5000400, Version5 EABI, hard-float ABI',
   '-AW':'Tag_ABI_VFP_args: VFP registers','-dW':'(NEEDED) Shared library: [libc.so.6]',
   '--dyn-syms':'1: 0000 4 FUNC GLOBAL DEFAULT 1 VSTPluginMain',
   '-VW':'Version needs section\nName: GLIBC_2.40'}[flags[0]]
 m.readelf=fake
 (p/'arm.so').write_bytes(b'\x7fELF')
 r=m.inspect(p/'arm.so',args,{'libc.so.6'})
 assert any('GLIBC 2.40' in s for s in r['blockers'])
 args.glibc='2.40'
 assert m.inspect(p/'arm.so',args,{'libc.so.6'})['verdict']=='CANDIDATE_REQUIRES_VERIFICATION'
 assert m.inspect(p/'arm.so',args,set())['blockers']
 m.readelf=original
print('Passed: five real ELF format fixtures, invalid file, ARM/GLIBC/dependency verdict logic')
