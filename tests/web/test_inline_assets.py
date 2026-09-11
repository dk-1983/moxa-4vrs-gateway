"""CSP hashes must match HTML-normalized inline text, including CRLF checkout."""
import base64,hashlib,json,re,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as d:
    d=Path(d);ui=d/'src/web/ui';ui.mkdir(parents=True);icons=d/'assets/icons';icons.mkdir(parents=True);help=d/'assets/help';help.mkdir()
    for name in ['index.html','app.js','extended.js','style.css','help-en.html','help-ru.html']:
        data=(root/'src/web/ui'/name).read_text(encoding='utf8').replace('\n','\r\n').encode()
        (ui/name).write_bytes(data)
    for name in ['favicon.ico','favicon-180.png']:(icons/name).write_bytes(b'fixture-image')
    for name in ['main-menu.png','configuration.png']:(help/name).write_bytes(b'fixture-image')
    subprocess.run([sys.executable,str(root/'tools/embed-web-assets.py'),str(d),str(icons),str(d/'out')],check=True)
    text=(d/'out/web-assets.c').read_text()
    csp=json.loads(re.search(r'web_inline_csp\[\]=(.*);',text).group(1))
    html=bytes(map(int,re.search(r'asset0\[\]=\{([0-9,]+)\}',text).group(1).split(','))).decode()
    for tag in ['script','style']:
        body=re.search('<'+tag+'>(.*?)</'+tag+'>',html,re.S).group(1).replace('\r\n','\n').replace('\r','\n')
        digest=base64.b64encode(hashlib.sha256(body.encode()).digest()).decode()
        assert "'sha256-"+digest+"'" in csp,(tag,csp)
    assert 'unsafe-inline' not in csp and not re.search(r'<script[^>]+src=|<link[^>]+href="/',html)
print('Inline CSS/JS CSP under CRLF checkout and compact root without parallel resources PASS')
