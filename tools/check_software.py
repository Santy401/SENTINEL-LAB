#!/usr/bin/env python3
"""Reproducible host checks; writes build artifacts only inside this repository."""
from pathlib import Path
import ast
import json
import os
import re
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
os.chdir(root)
(root/'build').mkdir(exist_ok=True)
env=dict(os.environ,TMPDIR=str(root/'build'))
def run(args, **kwargs):
    subprocess.run(args,check=True,env=env,**kwargs)
run([sys.executable,'scripts/check-repository.py'])
run([sys.executable,'scripts/check-conventional-commit.py','--self-test'])
def host(name,sources,includes=(),defines=()):
    run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude',*defines,*includes,*sources,'-o',f'build/{name}'])
    run([f'build/{name}'])
host('test_algorithm',['test/test_algorithm.cpp'])
host('test_motion_engine',['test/test_motion_engine.cpp','src/v02/motion_engine.cpp'])
host('test_rf',['test/test_rf.cpp'])
host('test_rf_monitor',['test/test_rf_monitor.cpp','src/rf/rf_monitor.cpp'],['-Itest/stubs'])
host('test_event_json',['test/test_event_json.cpp'],['-I.pio/libdeps/sentinel_esp32s3_n16r8_rf/ArduinoJson/src'])
host('test_motion_ingest',['test/test_motion_ingest.cpp','src/rf/motion_ingest.cpp','src/v02/motion_engine.cpp'],['-I.pio/libdeps/sentinel_esp32s3_n16r8_rf/ArduinoJson/src'])
ARDUINOJSON=['-I.pio/libdeps/sentinel_esp32s3_n16r8_rf/ArduinoJson/src']
host('test_frame_codec',['test/test_frame_codec.cpp','src/realtime/frame_codec.cpp'])
host('test_time_source',['test/test_time_source.cpp','src/realtime/time_source.cpp'])
host('test_realtime_limits',['test/test_realtime_limits.cpp','src/realtime/auth.cpp'])
host('test_handshake',['test/test_handshake.cpp','src/realtime/handshake.cpp'])
host('test_protocol_adapter',['test/test_protocol_adapter.cpp','src/realtime/protocol_adapter.cpp','src/realtime/time_source.cpp'],ARDUINOJSON)
host('test_session',['test/test_session.cpp','src/realtime/session.cpp','src/realtime/protocol_adapter.cpp','src/realtime/time_source.cpp'],ARDUINOJSON)
host('test_ws_server',['test/test_ws_server.cpp','src/realtime/ws_server.cpp','src/realtime/auth.cpp','src/realtime/frame_codec.cpp','src/realtime/handshake.cpp','src/realtime/protocol_adapter.cpp','src/realtime/session.cpp','src/realtime/time_source.cpp'],['-Itest/stubs']+ARDUINOJSON)
HEALTH_STUBS=['-Itest/health_stubs','test/health_stubs.cpp']
HEALTH_SOURCES=['src/realtime/realtime_core.cpp','src/realtime/auth.cpp','src/realtime/protocol_adapter.cpp','src/realtime/session.cpp','src/realtime/time_source.cpp']
WEB_SOURCES=['src/rf/web_rf.cpp','src/realtime/web_app.cpp','test/web_app_stub.cpp']+HEALTH_SOURCES
WEB_INCLUDES=['-Itest/web_stubs','-Itest/health_stubs','-Isrc']+ARDUINOJSON
host('test_health',['test/test_health.cpp']+HEALTH_SOURCES+HEALTH_STUBS,['-Itest/stubs']+ARDUINOJSON)
host('test_web_route_realtime',['test/test_web_route.cpp']+WEB_SOURCES+HEALTH_STUBS,WEB_INCLUDES,['-DSENTINEL_REALTIME=1'])
host('test_web_route_prod',['test/test_web_route.cpp']+WEB_SOURCES+HEALTH_STUBS,WEB_INCLUDES)
for path in ['src/v02/dashboard_html.h','src/rf/dashboard_html.h','src/rf/capture_html.h','src/rf/motion_setup_html.h']:
    text=Path(path).read_text()
    # Firefox iOS shows this native toast on shake-to-summarize; it must never be
    # synthesized by a page we serve, nor used to mask an API/motion failure.
    assert 'Resumen no disponible' not in text, f'Firefox toast text leaked into {path}'
    scripts=re.findall(r'<script>(.*?)</script>',text,re.S)
    for script in scripts:
        run(['node','--check'],input=script,text=True)
    ids=re.findall(r'\bid="([^"\s]+)"',text)
    assert len(ids)==len(set(ids)),f'duplicate DOM id in {path}'
    references=set(re.findall(r"(?:\$|document\.getElementById)\('([^']+)'\)",''.join(scripts)))
    assert references<=set(ids),(path,references-set(ids))
    if path=='src/rf/dashboard_html.h':
        # Real HTTP/motion failures stay explicit in #status; they are not
        # replaced by a generic browser-style toast.
        assert "SIN DATOS ACTUALES · '+e.message" in text
        assert "$('distance').textContent='No disponible'" in text
    print(f'{path}: JS syntax and {len(references)} DOM references OK')
for path in Path('tools').glob('*.py'):
    ast.parse(path.read_text())
workflow=json.loads(Path('integrations/n8n/sentinel-rf.workflow.json').read_text())
assert workflow['active'] is False
for node in workflow['nodes']:
    if node['type']=='n8n-nodes-base.code':
        run(['node','--check'],input='function check(){'+node['parameters']['jsCode']+'}',text=True)
print('Python syntax and inactive n8n workflow syntax OK')

run(['node','test/test_workflow.cjs'])

# Host emulator: the real RealtimeServer over loopback TCP (not hardware).
run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-Itest/posix_stubs',*ARDUINOJSON,
     'test/host_device/main.cpp','src/realtime/ws_server.cpp','src/realtime/auth.cpp',
     'src/realtime/frame_codec.cpp','src/realtime/handshake.cpp','src/realtime/protocol_adapter.cpp',
     'src/realtime/session.cpp','src/realtime/time_source.cpp','-o','build/host_device'])
run([sys.executable,'tools/tests/test_gateway.py','-v'])
if Path('apps/web/dist/index.html').is_file():
    run([sys.executable,'tools/embed_web.py','--check'])
print('Gateway unit, HTTP and emulator integration tests OK')
