from pathlib import Path
import re
from generate_coefficients import coefficients
source=Path(__file__).resolve().parents[2]/'ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_pcm_fixture.cpp'
text=source.read_text().split('constexpr int16_t Coefficients[2][Taps]={',1)[1].split('\n};',1)[0]
text=re.sub(r'//[^\n]*','',text)
actual=list(map(int,re.findall(r'-?\d+',text)))
expected=[v for row in coefficients() for v in row]
assert actual==expected
print('PASS 98 checked-in Q15 coefficients reproduce offline design exactly')
