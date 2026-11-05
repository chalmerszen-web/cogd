from pathlib import Path
from urllib.parse import urljoin
from html.parser import HTMLParser
import urllib.request,sys
class Links(HTMLParser):
    def __init__(self):super().__init__();self.links=[]
    def handle_starttag(self,tag,attrs):
        if tag=='a':self.links.extend(v for k,v in attrs if k=='href')
url=sys.argv[1];raw=urllib.request.urlopen(url,timeout=30).read()
parser=Links();parser.feed(raw.decode('utf8','replace'))
for link in sorted(set(parser.links)):
    if any(w in link.lower() for w in ('github','resources','schematic','.pdf','.zip','esp-idf')):print(urljoin(url,link))
