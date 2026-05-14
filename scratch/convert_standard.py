import re
import os
import json
import zipfile

def convert_html(old_html):
    lines = old_html.splitlines()
    name = "Converted Dictionary"
    index_lang = "English"
    content_lang = "English"
    
    entries = []
    current_hw = None
    current_content = []
    
    state = 'META'
    
    for line in lines:
        if state == 'META':
            if line.startswith('#NAME'):
                name = line.split('"')[1]
            elif line.startswith('#INDEX_LANGUAGE'):
                index_lang = line.split('"')[1]
            elif line.startswith('#CONTENTS_LANGUAGE'):
                content_lang = line.split('"')[1]
            elif line.strip() and not line.startswith('#'):
                current_hw = line.strip()
                state = 'HTML'
        elif state == 'HTML':
            if line.strip() == '</>':
                entries.append((current_hw, "\n".join(current_content)))
                current_hw = None
                current_content = []
                state = 'META'
            else:
                current_content.append(line)
    
    new_html_parts = []
    for hw, content in entries:
        new_html_parts.append(f'<article id="{hw}" class="entry">\n{content}\n</article>')
    
    new_html = "\n".join(new_html_parts)
    meta = {
        "format": 1,
        "id": name.lower().replace(" ", "_"),
        "name": name,
        "short_name": name[:10],
        "index_languages": [index_lang],
        "content_languages": [content_lang],
        "version": "1.0",
        "stylesheet": "style.css",
        "html": "dictionary.html"
    }
    
    return new_html, meta

def convert_diction(path):
    with zipfile.ZipFile(path, 'r') as zin:
        old_html = zin.read('dictionary.html').decode('utf-8')
        new_html, meta = convert_html(old_html)
        
        stem = os.path.basename(path).replace(".diction", "")
        out_path = path.replace(".diction", "_new.diction")
        
        with zipfile.ZipFile(out_path, 'w') as zout:
            # We want a top-level dir
            root = stem + "/"
            zout.writestr(root + "dictionary.html", new_html)
            zout.writestr(root + "meta.json", json.dumps(meta, indent=2))
            zout.writestr(root + "style.css", "body { font-family: sans-serif; }")
            
            for item in zin.infolist():
                if item.filename != 'dictionary.html':
                    content = zin.read(item.filename)
                    zout.writestr(root + item.filename, content)
    
    print(f"Converted {path} to {out_path}")

if __name__ == "__main__":
    convert_diction("sampledict/sampledict.diction")
