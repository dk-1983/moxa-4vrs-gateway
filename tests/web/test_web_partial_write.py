"""Actual TLS/plain HTTP under deterministic partial send/EAGAIN/EINTR."""
from pathlib import Path
import sys
source=Path(sys.argv[2]);helpers=(source/'tests/web/test_web_integration.py').read_text()
exec(compile(helpers[:helpers.index('\ntry:\n start();')],str(source/'tests/web/test_web_integration.py'),'exec'))
try:
 start();command('enable');wait(lambda:request('/api/hello',auth=False)[0]==200)
 s,b,h=request('/app.js',auth=False);assert s==200 and b==(source/'src/web/ui/app.js').read_bytes()
 code=wait(lambda:(root/'display').read_text() if (root/'display').exists() else '')
 api('enroll',{'password':'public-partial-write-test','code':code},auth=False)
 s,b,h=request('/help/configuration.png');assert s==200 and b==(source/'assets/help/configuration.png').read_bytes()
 command('http');wait(lambda:request('/api/hello',secure=False,auth=False)[0]==200)
 s,b,h=request('/app.js',secure=False,auth=False);assert s==200 and b==(source/'src/web/ui/app.js').read_bytes()
 print('partial send/EAGAIN/EINTR: exact TLS asset/authenticated image and HTTP asset bytes PASS')
finally:stop();log.close()
