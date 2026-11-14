import sys
from PIL import Image
T='/workspace/climbmod/rope_slice/textures'
for n in sys.argv[1:]:
    ao,ro,me=[Image.open(f'{T}/{n}_{t}.png').convert('L') for t in('ao','rough','metal')]
    Image.merge('RGB',(ao,ro,me)).save(f'{T}/{n}_orm.png');print('ORM',n)
