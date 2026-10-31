"""Buildless documentation integrity checks (no network or third-party packages)."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit, unquote

ROOT = Path(__file__).resolve().parents[1] / 'docs'

class Page(HTMLParser):
    def __init__(self, path):
        super().__init__(convert_charrefs=True)
        self.path, self.ids, self.refs, self.headings = path, set(), [], 0
        self.current_nav = 0
        self.feed(path.read_text())
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a:
            assert a['id'] not in self.ids, (self.path, 'duplicate id', a['id'])
            self.ids.add(a['id'])
        if tag == 'h1': self.headings += 1
        if tag == 'a' and a.get('aria-current') == 'page': self.current_nav += 1
        if tag in ('a', 'link', 'script', 'img'):
            value = a.get('href', a.get('src', ''))
            if value: self.refs.append(value)
        if tag == 'img': assert 'alt' in a, (self.path, 'image missing alt')

pages = {p.resolve(): Page(p) for p in ROOT.rglob('*.html')}
for path, page in pages.items():
    assert page.headings == 1, (path, 'expected one h1')
    assert page.current_nav == 1, (path, 'expected one current navigation item')
    assert 'main-content' in page.ids, (path, 'missing skip destination')
    assert 'bytes omitted' not in path.read_text(), (path, 'truncated content')
    for ref in page.refs:
        u = urlsplit(ref)
        if u.scheme or u.netloc: continue
        target = (path.parent / unquote(u.path)).resolve() if u.path else path
        if target.is_dir(): target /= 'index.html'
        assert target.exists(), (path, 'missing local target', ref)
        if u.fragment and target in pages:
            assert unquote(u.fragment) in pages[target].ids, (path, 'missing anchor', ref)
mini = pages[(ROOT / 'mini.html').resolve()]
assert all('page-' + str(n) in mini.ids for n in range(1, 7)), 'Mini must show all six pages'
assert 'install' in mini.ids
print(f'PASS: {len(pages)} pages; headings, navigation, anchors, local assets, and six Mini pages intact')
