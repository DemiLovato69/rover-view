"""Rebuild the OGL-Ontario raster MBTiles covering the synthetic practice pad."""
import math
import os
import sqlite3
import tempfile
import urllib.request
from pathlib import Path

SERVICE = ('https://ws.lioservices.lrc.gov.on.ca/arcgis2/rest/services/LIO_Imagery/'
           'Ontario_Imagery_Web_Map_Service/MapServer/tile')

def build():
    n = 2**17
    x = int((-75.93252 + 180) / 360 * n)
    y = int((1 - math.asinh(math.tan(math.radians(45.28558))) / math.pi) / 2 * n)
    def lon(i): return i/n*360-180
    def lat(j): return math.degrees(math.atan(math.sinh(math.pi*(1-2*j/n))))
    dst = Path(__file__).resolve().parents[1] / 'fixtures/practice-aerial.mbtiles'
    with tempfile.TemporaryDirectory(dir=dst.parent) as tmp:
        path = Path(tmp) / dst.name
        db = sqlite3.connect(path)
        try:
            db.executescript('CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT);'
                             'CREATE TABLE tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB, PRIMARY KEY(zoom_level,tile_column,tile_row));')
            db.executemany('INSERT INTO metadata VALUES(?,?)', dict(name='Practice pad · Ontario orthophoto',format='jpg',date='2024 (DRAPE)', attribution='Contains information licensed under the Open Government Licence – Ontario. Geospatial Ontario, DRAPE 2024.',bounds=f'{lon(x)},{lat(y+1)},{lon(x+2)},{lat(y)}',minzoom='17',maxzoom='19').items())
            for z in range(17,20):
                factor = 2**(z-17)
                for tx in range(x*factor,(x+2)*factor):
                    for ty in range(y*factor,(y+1)*factor):
                        request = urllib.request.Request(f'{SERVICE}/{z}/{ty}/{tx}',headers={'User-Agent':'FieldRover practice fixture/0.1'})
                        with urllib.request.urlopen(request,timeout=20) as response:
                            data = response.read(1024*1024)
                            if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
                                raise ValueError(f'Expected JPEG at {z}/{tx}/{ty}')
                        db.execute('INSERT INTO tiles VALUES(?,?,?,?)',(z,tx,2**z-1-ty,data))
            db.commit()
            assert db.execute('SELECT count(*) FROM tiles').fetchone()==(42,)
        finally:
            db.close()
        os.replace(path,dst)

if __name__=='__main__': build()
