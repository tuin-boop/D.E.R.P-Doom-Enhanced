"""Generate the compact switch gallery: switch_gallery.py base.wad output.wad."""
import sys
from material_gallery import make

if __name__ == '__main__':
    make(*sys.argv[1:], switches=True)
