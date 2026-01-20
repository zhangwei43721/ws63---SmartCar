#!/usr/bin/env python3
"""合并 index.html, app.js, style.css 为单个 index.html"""

import html

# 读取文件
with open('style.css', 'r', encoding='utf-8') as f:
    css_content = f.read()

with open('app.js', 'r', encoding='utf-8') as f:
    js_content = f.read()

with open('index.html', 'r', encoding='utf-8') as f:
    html_content = f.read()

# 替换 link 标签为内联 style
html_content = html_content.replace('<link rel="stylesheet" href="./style.css" />',
                                     f'<style>{css_content}</style>')

# 替换 script 标签为内联 script
html_content = html_content.replace('<script src="./app.js"></script>',
                                     f'<script>{js_content}</script>')

# 写入新的 index.html
with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html_content)

print("合并完成！index.html 已更新")
