# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 用独立解码器校验生产代码生成的240px二维码。/ Independently decode production-generated 240px QR images.
from pathlib import Path
from PIL import Image
import zxingcpp

samples = {
    "url-ap": "http://192.168.4.1",
    "url-sta": "http://192.168.123.234",
    "url-weread": "http://192.168.50.123/manage#key=0123456789abcdef0123456789abcdef&view=weread",
    "url-home": "http://192.168.50.123/h#t=0123456789abcdef0123456789abcdef",
    "weread-reader": "https://weread.qq.com/web/reader/demoBookId",
    "weread-detail": "https://weread.qq.com/web/bookDetail/demoBookId",
    "normal": "WIFI:T:WPA;S:ReadPico-5945;P:readpico;;",
    "escaped": 'WIFI:T:WPA;S:Pico\\;\\,\\:\\\\\\";P:pass\\;\\,\\:\\\\\\";;',
}
for name, expected in samples.items():
    path = Path("build") / f"wifi-qr-{name}.pgm"
    picture = Image.open(path)
    result = zxingcpp.read_barcode(picture)
    assert result is not None and result.text == expected, name
    assert picture.size == (240, 240)
    print(f"{name}: independent QR decode passed")
