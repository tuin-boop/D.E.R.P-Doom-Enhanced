"""Regenerate embedded portrait/PCM headers from the supplied local assets."""
from pathlib import Path
root = Path(__file__).parent
data = (root/'portrait.png').read_bytes()
(root/'portrait.h').write_text('/* Original full-resolution PNG, embedded unchanged. */\nstatic const unsigned char toasty_png[]={\n'+ '\n'.join(','.join(str(x) for x in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n')
data = (root/'voice.u8').read_bytes()
(root/'voice.h').write_text('/* 22050 Hz mono unsigned PCM; trimmed silence. */\nstatic const unsigned char toasty_voice[]={\n'+ '\n'.join(','.join(str(x) for x in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n')

