import re, html, os, sys

def esc(s):
    return html.escape(s, quote=False)

def inline(s):
    s = esc(s)
    s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)
    return s

def slug(s):
    return re.sub(r'[^a-z0-9\u4e00-\u9fff]+','-', s.lower()).strip('-')

def convert(md):
    lines = md.split('\n')
    out, toc = [], []
    i = 0
    in_code = False
    code_lang = ''
    code_buf = []
    while i < len(lines):
        line = lines[i]
        if line.startswith('```'):
            if not in_code:
                in_code = True; code_buf = []
                code_lang = line[3:].strip()
            else:
                in_code = False
                out.append('<pre class="code"><code>' + esc('\n'.join(code_buf)) + '</code></pre>')
            i += 1; continue
        if in_code:
            code_buf.append(line); i += 1; continue

        m = re.match(r'^(#{1,3})\s+(.*)$', line)
        if m:
            lvl, txt = len(m.group(1)), m.group(2)
            sid = slug(txt)
            if lvl == 1:
                out.append(f'<h1 id="{sid}">{inline(txt)}</h1>')
            else:
                out.append(f'<h{lvl} id="{sid}">{inline(txt)}</h{lvl}>')
                if lvl == 2:
                    toc.append((sid, txt))
            i += 1; continue

        if line.startswith('|') and i + 1 < len(lines) and re.match(r'^\|[\s:|-]+\|$', lines[i+1]):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                rows.append(cells); i += 1
            head, body = rows[0], rows[2:]
            t = '<table><thead><tr>' + ''.join(f'<th>{inline(c)}</th>' for c in head) + '</tr></thead><tbody>'
            for r in body:
                t += '<tr>' + ''.join(f'<td>{inline(c)}</td>' for c in r) + '</tr>'
            out.append(t + '</tbody></table>')
            continue

        if line.startswith('> '):
            buf = []
            while i < len(lines) and lines[i].startswith('>'):
                buf.append(lines[i][2:]); i += 1
            out.append('<div class="callout">' + inline(' '.join(buf)) + '</div>')
            continue

        if re.match(r'^\d+\.\s+', line):
            items = []
            while i < len(lines) and re.match(r'^\d+\.\s+', lines[i]):
                items.append(re.sub(r'^\d+\.\s+', '', lines[i])); i += 1
            out.append('<ol>' + ''.join(f'<li>{inline(x)}</li>' for x in items) + '</ol>')
            continue

        if line.startswith('- '):
            items = []
            while i < len(lines) and lines[i].startswith('- '):
                items.append(lines[i][2:]); i += 1
            out.append('<ul>' + ''.join(f'<li>{inline(x)}</li>' for x in items) + '</ul>')
            continue

        if line.strip() == '':
            i += 1; continue

        buf = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(('#','```','|','> ','- ')) and not re.match(r'^\d+\.\s+', lines[i]):
            buf.append(lines[i]); i += 1
        out.append('<p>' + inline(' '.join(buf)) + '</p>')
    return '\n'.join(out), toc

TPL = '''<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} - wcteos 测试版</title>
<style>
:root{{--paper:#efe9da;--ink:#2a2724;--accent:#34568a;--line:#cfc7b4;--code:#25282e;--muted:#6f6a60}}
*{{box-sizing:border-box}}
body{{margin:0;background:var(--paper);color:var(--ink);font:16px/1.75 "Noto Serif CJK SC","Source Han Serif SC",Georgia,serif}}
.layout{{display:grid;grid-template-columns:250px minmax(0,1fr);gap:36px;max-width:1180px;margin:0 auto;padding:32px 24px 80px}}
aside{{position:sticky;top:24px;align-self:start;max-height:calc(100vh - 48px);overflow:auto}}
.brand{{font:600 13px/1.4 system-ui,sans-serif;letter-spacing:.12em;color:var(--muted);text-transform:uppercase;margin-bottom:14px}}
nav a{{display:block;font:14px/1.5 system-ui,sans-serif;color:var(--ink);text-decoration:none;padding:5px 0 5px 10px;border-left:2px solid transparent}}
nav a:hover{{color:var(--accent);border-left-color:var(--accent)}}
.back{{font:13px system-ui,sans-serif;color:var(--muted);text-decoration:none;display:inline-block;margin-top:22px}}
main{{background:#f7f3e9;border:1px solid var(--line);padding:34px 42px 48px;box-shadow:4px 4px 0 #d8d1bf}}
h1{{font-size:30px;line-height:1.3;margin:0 0 20px;padding-bottom:14px;border-bottom:3px double var(--line)}}
h2{{font-size:21px;margin:34px 0 10px;color:var(--accent);padding-bottom:6px;border-bottom:1px solid var(--line)}}
h3{{font-size:17px;margin:22px 0 8px}}
p{{margin:10px 0}}
code{{background:#e7e0cf;padding:1px 5px;border-radius:2px;font:13.5px/1.5 "JetBrains Mono",Consolas,monospace}}
pre.code{{background:var(--code);color:#e6e8ea;padding:16px 18px;overflow:auto;border-radius:3px;margin:14px 0;font:13.5px/1.7 "JetBrains Mono",Consolas,monospace}}
pre.code code{{background:none;padding:0;color:inherit;font-size:inherit}}
table{{border-collapse:collapse;width:100%;margin:14px 0;font:14.5px/1.6 system-ui,sans-serif}}
th,td{{border:1px solid var(--line);padding:8px 10px;text-align:left;vertical-align:top}}
th{{background:#e7e0cf;font-weight:600}}
ul,ol{{margin:10px 0 10px 22px}}
.callout{{background:#e9e2cf;border-left:4px solid var(--accent);padding:12px 16px;margin:16px 0;font:14.5px/1.7 system-ui,sans-serif}}
footer{{margin-top:44px;padding-top:16px;border-top:1px solid var(--line);font:13px system-ui,sans-serif;color:var(--muted)}}
footer a{{color:var(--accent);text-decoration:none;margin-right:14px}}
@media(max-width:820px){{.layout{{grid-template-columns:1fr}}aside{{position:static;max-height:none}}main{{padding:24px 18px}}}}
</style></head><body><div class="layout">
<aside><div class="brand">wcteos 测试版 · 文档</div><nav>{nav}</nav><a class="back" href="../index.html">← 返回首页</a></aside>
<main>{body}
<footer><a href="wct-compile.html">编译手册</a><a href="terminal.html">终端手册</a><a href="performance.html">运行报告</a><span>仓库 github.com/ETQWFD/wcteos-test</span></footer>
</main></div></body></html>'''

def build(md_path, out_path, title):
    txt, toc = convert(open(md_path, encoding='utf-8').read())
    nav = '\n'.join(f'<a href="#{sid}">{esc(t)}</a>' for sid, t in toc)
    open(out_path, 'w', encoding='utf-8').write(TPL.format(title=title, nav=nav, body=txt))

if __name__ == '__main__':
    build('docs/md/wct-compile.md', 'docs/wct-compile.html', '.wct 编译手册')
    build('docs/md/terminal.md', 'docs/terminal.html', '终端使用手册')
    build('docs/md/performance.md', 'docs/performance.html', '内核运行报告')
    print('built')
