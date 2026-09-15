"""Fetch a pinned vendor source candidate without treating it as target provenance."""
import hashlib
import json
import urllib.request
from pathlib import Path

out = Path(__file__).resolve().parent
commit = '17ae1445226e5b3ebd76c15ce30b8f894ab79a84'
paths = ['Makefile', 'drivers/net/can/rockchip/rockchip_canfd.c',
         'drivers/net/can/dev/netlink.c', 'drivers/net/can/dev/skb.c',
         'net/can/raw.c', 'net/can/af_can.c']
manifest = {'repository': 'LubanCat/kernel', 'branch': 'lbc-develop-6.1',
            'commit': commit, 'target_match': 'UNVERIFIED', 'files': {}}
for path in paths:
    url = 'https://raw.githubusercontent.com/LubanCat/kernel/' + commit + '/' + path
    data = urllib.request.urlopen(url, timeout=20).read()
    destination = out / 'vendor_candidate' / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    manifest['files'][path] = {'url': url, 'sha256': hashlib.sha256(data).hexdigest()}
    print(path, len(data), flush=True)
(out / 'candidate_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
