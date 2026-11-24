# Inlines dsp.js and ../Resources/presets.json into app.html -> index.html (the single-file web version)
import pathlib
here = pathlib.Path(__file__).parent
d = (here / 'dsp.js').read_text(encoding='utf-8')
presets = (here.parent / 'Resources' / 'presets.json').read_text(encoding='utf-8')
html = (here / 'app.html').read_text(encoding='utf-8').replace('/*__DSP__*/', d).replace('/*__PRESETS__*/', presets)
(here / 'index.html').write_text(html, encoding='utf-8')
print('built index.html')
