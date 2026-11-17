// 把 docs/src 下的 mdBook Markdown 转成可直接双击打开的静态 HTML
const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', 'src');
const OUT = path.join(__dirname, '..', '..', 'docs-preview');

function rmrf(p) {
  if (fs.existsSync(p)) fs.rmSync(p, { recursive: true, force: true });
}

// 极简 Markdown → HTML
function md2html(md) {
  const lines = md.split(/\r?\n/);
  let out = [];
  let inCode = false;
  let inList = false;
  let listType = '';

  const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

  function inline(s) {
    // 先转义，再处理行内标记
    s = esc(s);
    // 行内代码
    s = s.replace(/`([^`]+)`/g, '<code>$1</code>');
    // 粗体
    s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    // 链接 [text](url)
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, (m, t, u) => {
      // .md 链接改 .html
      u = u.replace(/\.md(#.*)?$/, (mm, hash) => '.html' + (hash || ''));
      return `<a href="${u}">${t}</a>`;
    });
    return s;
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    // 代码块
    if (/^\s{4,}\S/.test(line)) {
      if (!inCode) {
        // 收集整个缩进块
        const buf = [];
        while (i < lines.length && (/^\s{4,}/.test(lines[i]) || lines[i].trim() === '')) {
          const l = lines[i];
          if (l.trim() === '') {
            // 空行：看下一行还是缩进块吗
            if (i + 1 < lines.length && /^\s{4,}/.test(lines[i + 1])) { buf.push(''); i++; continue; }
            else break;
          }
          buf.push(l.substring(4));
          i++;
        }
        i--;
        out.push('<pre><code>' + esc(buf.join('\n')) + '</code></pre>');
        inCode = true;
        continue;
      }
    }
    inCode = false;

    // 围栏代码块
    if (/^```/.test(line)) {
      const buf = [];
      i++;
      while (i < lines.length && !/^```/.test(lines[i])) { buf.push(lines[i]); i++; }
      out.push('<pre><code>' + esc(buf.join('\n')) + '</code></pre>');
      continue;
    }

    // 标题
    let m;
    if ((m = line.match(/^(#{1,6})\s+(.*)$/))) {
      if (inList) { out.push(listType === 'ul' ? '</ul>' : '</ol>'); inList = false; }
      const lv = m[1].length;
      out.push(`<h${lv}>${inline(m[2])}</h${lv}>`);
      continue;
    }

    // 分隔线
    if (/^---+\s*$/.test(line)) { out.push('<hr>'); continue; }

    // 列表
    if ((m = line.match(/^\s*[-*]\s+(.*)$/))) {
      if (!inList || listType !== 'ul') {
        if (inList) out.push(listType === 'ul' ? '</ul>' : '</ol>');
        out.push('<ul>'); inList = true; listType = 'ul';
      }
      out.push(`<li>${inline(m[1])}</li>`);
      continue;
    }
    if ((m = line.match(/^\s*\d+\.\s+(.*)$/))) {
      if (!inList || listType !== 'ol') {
        if (inList) out.push(listType === 'ul' ? '</ul>' : '</ol>');
        out.push('<ol>'); inList = true; listType = 'ol';
      }
      out.push(`<li>${inline(m[1])}</li>`);
      continue;
    }

    if (inList && line.trim() !== '') {
      // 列表续行
      out.push(`<li style="list-style:none;margin-left:-1.2em">${inline(line)}</li>`);
      continue;
    }
    if (inList && line.trim() === '') { out.push(listType === 'ul' ? '</ul>' : '</ol>'); inList = false; continue; }

    // 表格
    if (/^\s*\|.*\|\s*$/.test(line)) {
      const rows = [];
      while (i < lines.length && /^\s*\|.*\|\s*$/.test(lines[i])) { rows.push(lines[i]); i++; }
      i--;
      let tbl = '<table>';
      rows.forEach((r, idx) => {
        const cells = r.trim().replace(/^\||\|$/g, '').split('|').map(c => c.trim());
        if (idx === 1 && cells.every(c => /^:?-+:?$/.test(c))) return; // 分隔行
        const tag = idx === 0 ? 'th' : 'td';
        tbl += '<tr>' + cells.map(c => `<${tag}>${inline(c)}</${tag}>`).join('') + '</tr>';
      });
      tbl += '</table>';
      out.push(tbl);
      continue;
    }

    // 引用
    if ((m = line.match(/^>\s*(.*)$/))) {
      out.push(`<blockquote>${inline(m[1])}</blockquote>`);
      continue;
    }

    // 空行
    if (line.trim() === '') { continue; }

    // 普通段落
    out.push(`<p>${inline(line)}</p>`);
  }
  if (inList) out.push(listType === 'ul' ? '</ul>' : '</ol>');
  return out.join('\n');
}

// 解析 SUMMARY.md 得到目录树
function parseSummary(summaryText) {
  const lines = summaryText.split(/\r?\n/);
  const items = [];
  for (const line of lines) {
    let m;
    if ((m = line.match(/^#\s+(.*)$/))) { items.push({ type: 'part', text: m[1] }); continue; }
    if ((m = line.match(/^\s*[-*]\s+\[([^\]]+)\]\(([^)]+)\)/))) {
      items.push({ type: 'page', text: m[1], path: m[2] });
    }
  }
  return items;
}

// 主流程
rmrf(OUT);
fs.mkdirSync(OUT, { recursive: true });

const summaryPath = path.join(SRC, 'SUMMARY.md');
const items = parseSummary(fs.readFileSync(summaryPath, 'utf8'));
const pages = items.filter(x => x.type === 'page');

// 构建侧边栏
let sidebar = '<div class="sidebar">\n<div class="book-title">YXG 语言教程</div>\n';
for (const it of items) {
  if (it.type === 'part') {
    sidebar += `<div class="part">${it.text}</div>\n`;
  } else {
    const href = it.path.replace(/\.md$/, '.html');
    sidebar += `<a class="page" href="${href}">${it.text}</a>\n`;
  }
}
sidebar += '</div>\n';

// 找所有 md 文件
function walk(dir, base = '') {
  const res = [];
  for (const f of fs.readdirSync(dir, { withFileTypes: true })) {
    const rel = base ? base + '/' + f.name : f.name;
    if (f.isDirectory()) res.push(...walk(path.join(dir, f.name), rel));
    else if (f.name.endsWith('.md')) res.push(rel);
  }
  return res;
}

const allMd = walk(SRC);
let count = 0;

for (const rel of allMd) {
  if (rel === 'SUMMARY.md') continue;
  const md = fs.readFileSync(path.join(SRC, rel), 'utf8');
  // 去掉首个 H1（侧边栏已显示），保留正文
  const body = md2html(md);
  const depth = rel.split('/').length - 1;
  const up = depth > 0 ? '../'.repeat(depth) : '';
  const cssDepth = up;

  const html = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>${rel.replace(/\.md$/, '')} - YXG 语言教程</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;line-height:1.7;color:#333;display:flex}
a{color:#0366d6;text-decoration:none}
a:hover{text-decoration:underline}
.sidebar{width:280px;flex-shrink:0;height:100vh;overflow-y:auto;background:#f6f7f9;border-right:1px solid #e1e4e8;padding:16px 0;position:sticky;top:0}
.book-title{font-weight:700;font-size:16px;padding:0 16px 12px;color:#24292e;border-bottom:1px solid #e1e4e8;margin-bottom:8px}
.part{font-size:12px;font-weight:700;color:#6a737d;padding:12px 16px 4px;text-transform:none}
.page{display:block;padding:4px 16px 4px 24px;font-size:14px;color:#444}
.page:hover{background:#e8eaed;text-decoration:none}
.content{flex:1;max-width:900px;padding:32px 48px 120px;overflow-x:hidden}
h1{font-size:2em;border-bottom:1px solid #eaecef;padding-bottom:.3em;margin-top:0}
h2{font-size:1.5em;border-bottom:1px solid #eaecef;padding-bottom:.3em;margin-top:1.5em}
h3{font-size:1.25em;margin-top:1.4em}
h4{font-size:1.05em;margin-top:1.2em}
pre{background:#f6f8fa;padding:16px;overflow-x:auto;border-radius:6px;font-size:13px;line-height:1.45}
code{font-family:Consolas,"Courier New",monospace;background:#f0f1f3;padding:2px 5px;border-radius:3px;font-size:.9em}
pre code{background:none;padding:0;font-size:inherit}
table{border-collapse:collapse;margin:16px 0;display:block;overflow-x:auto}
th,td{border:1px solid #dfe2e5;padding:6px 13px}
th{background:#f6f8fa;font-weight:600}
tr:nth-child(2n){background:#f9fafb}
blockquote{border-left:4px solid #dfe2e5;margin:16px 0;padding:0 16px;color:#6a737d;background:#fbfbfc}
hr{border:0;border-top:1px solid #eaecef;margin:24px 0}
ul,ol{padding-left:2em}
li{margin:.25em 0}
</style>
</head>
<body>
${sidebar.replace(/href="/g, `href="${up}`)}
<div class="content">
${body}
</div>
</body>
</html>`;

  const outp = path.join(OUT, rel.replace(/\.md$/, '.html'));
  fs.mkdirSync(path.dirname(outp), { recursive: true });
  fs.writeFileSync(outp, html, 'utf8');
  count++;
}

// 首页跳到 preface
const first = pages.length ? pages[0].path.replace(/\.md$/, '.html') : 'preface.html';
fs.writeFileSync(path.join(OUT, 'index.html'),
  `<!DOCTYPE html><html><head><meta charset="UTF-8"><meta http-equiv="refresh" content="0;url=${first}"></head><body><a href="${first}">进入教程</a></body></html>`,
  'utf8');

console.log('生成完成: ' + count + ' 个页面');
console.log('输出目录: ' + OUT);
console.log('入口: ' + path.join(OUT, 'index.html'));
