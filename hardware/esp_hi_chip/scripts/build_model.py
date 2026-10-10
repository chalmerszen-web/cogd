"""Single source of truth for the new board's electrical connections.

This file does not claim to be an EDA/DRC result. Library pin mappings and final
placement are checked in the editor before a manufacturing release.
"""
from pathlib import Path
import csv
import json

ROOT = Path(__file__).resolve().parents[1]
parts = []


def add(ref, value, footprint, nets, block, xy, *, mpn='', note='', fitted=True):
    parts.append(dict(ref=ref, value=value, footprint=footprint,
                      pins={str(k): v for k, v in nets.items()}, block=block,
                      pcb_mm=list(xy), mpn=mpn, note=note, fitted=fitted))


def two(ref, value, a, b, block, xy, footprint='0402', **kw):
    add(ref, value, footprint, {1: a, 2: b}, block, xy, **kw)


# ESP32-C3FH4X (not a module, not the external-flash C3 pinout).
add('U1', 'ESP32-C3FH4X', 'QFN32_5x5_EP', {
    1:'RF_CHIP', 2:'RF_3V3', 3:'RF_3V3', 4:'WAKE_MOVE', 5:'WAKE_AUDIO',
    6:'MIC_ADC', 7:'EN', 8:'PA_EN', 9:'LCD_MOSI', 10:'LCD_SCLK',
    11:'+3V3', 12:'PDM_P', 13:'PDM_N', 14:'RGB_3V3', 15:'BOOT_N',
    16:'LCD_DC', 17:'+3V3', 18:'VDD_SPI',
    19:None,20:None,21:None,22:None,23:None,24:None,
    25:'USB_DM_MCU',26:'USB_DP_MCU',27:'UART_RX',28:'UART_TX',
    29:'XTAL_N',30:'XTAL_P',31:'+3V3',32:'+3V3',33:'GND'
}, 'core', (17,18), mpn='ESP32-C3FH4X')
two('C1','100nF','+3V3','GND','core',(16,21.4))
two('C2','100nF','+3V3','GND','core',(20.3,20.4))
two('C3','1uF','VDD_SPI','GND','core',(20.5,18.1))
two('C4','1uF','+3V3','GND','core',(18,14.3))
two('C5','10nF','+3V3','GND','core',(16.5,14.3))
two('L1','2.0nH >=500mA','+3V3','RF_3V3','core',(11.9,15.1))
two('C6','10uF 10V X5R','+3V3','GND','core',(11.2,13.3),'0603')
two('C7','100nF','+3V3','GND','core',(13,13.3))
two('C8','1uF','RF_3V3','GND','core',(12.8,17.1))
add('Y1','40MHz CL10pF +/-10ppm','Crystal_2520_4P',
    {1:'XTAL_LOAD',2:'GND',3:'XTAL_N',4:'GND'},'core',(21.8,14.7),
    mpn='FA-20H 40.0000MF10Z-K3',
    note='Epson 2.5x2.0mm RF reference crystal, CL10pF; initial 15pF loads assume 2.5pF stray. Measure offset.')
two('L2','24nH','XTAL_P','XTAL_LOAD','core',(20,13.2))
two('C9','15pF C0G','XTAL_LOAD','GND','core',(21.8,12.6))
two('C10','15pF C0G','XTAL_N','GND','core',(23.7,15.1))
two('R1','10k','+3V3','EN','core',(15.1,12.5))
two('C11','1uF','EN','GND','core',(14.7,14.4))
two('R2','10k','+3V3','BOOT_N','core',(18,22.8))
two('R3','10k','+3V3','RGB_3V3','core',(16,22.8))
two('R4','100k','PA_EN','GND','core',(13.3,20.3))
add('SW1','BOOT','SMD_Tactile_2P',{1:'BOOT_N',2:'GND'},'io',(31,21))
add('SW2','RESET','SMD_Tactile_2P',{1:'EN',2:'GND'},'io',(31,16.5))
add('SW3','WAKE','SMD_Tactile_2P',{1:'WAKE_MOVE',2:'GND'},'io',(3,20))
two('R5','10k','+3V3','WAKE_MOVE','io',(6.1,19))
two('R6','10k','+3V3','WAKE_AUDIO','io',(6.1,21))

# RF path: no PCB antenna beneath the LCD metal backplate.
two('R7','0R RF tuning link','RF_CHIP','RF_50','rf',(9.9,17.7),
    note='Initial bring-up value only; replace with tuned series L after VNA/RF measurements.')
two('C12','DNP RF tuning','RF_CHIP','GND','rf',(11.2,18.9),fitted=False)
two('C13','DNP RF tuning','RF_50','GND','rf',(8.5,18.9),fitted=False)
add('J6','External 2.4GHz antenna','U.FL_SMT',{1:'RF_50',2:'GND'},'rf',(6,15.5),
    mpn='U.FL-R-SMT-1(10)',note='KiCad Hirose footprint: pad 1 signal, both pad 2 lands ground; antenna outside display metal.')

# USB-C: all-SMT housing tabs; no stakes protruding into the display face.
usb_pins = {p:'GND' for p in ['A1','A12','B1','B12','S1','S2','S3','S4']}
usb_pins.update({p:'VBUS' for p in ['A4','A9','B4','B9']})
usb_pins.update({'A5':'CC1','B5':'CC2','A6':'USB_DP','B6':'USB_DP',
                 'A7':'USB_DM','B7':'USB_DM','A8':None,'B8':None})
add('J1','USB-C USB2 all-SMT','USB4110',usb_pins,'power',(17.15,3.2),mpn='USB4110-GF-A')
two('R8','5.1k 1%','CC1','GND','power',(10.5,5.1))
two('R9','5.1k 1%','CC2','GND','power',(23.6,5.1))
two('R10','22R','USB_DM','USB_DM_MCU','power',(20.4,10.7))
two('R11','22R','USB_DP','USB_DP_MCU','power',(21.8,10.7))
add('D1','USB ESD','SOT23-6',{1:'USB_DM',2:'GND',3:'USB_DP',4:'USB_DP',5:'VBUS',6:'USB_DM'},
    'power',(17.5,7.4),mpn='USBLC6-2SC6')
add('U2','3.392V buck 2A','TSOT26',{1:'BUCK_FB',2:'VBUS',3:'VBUS',4:'GND',5:'SW_BUCK',6:'BST'},
    'power',(28,9),mpn='AP63200WU-7',
    note='0.8V*(1+32.4k/10k)=3.392V. Shared regulated core/RGB/audio rail avoids raw USB overvoltage on 5V-only parts.')
two('R32','32.4k 0.1%','+3V3','BUCK_FB','power',(26,11.4))
two('R33','10k 0.1%','BUCK_FB','GND','power',(24.5,11.4))
two('C14','10uF 10V X5R','VBUS','GND','power',(29,5.8),'0603')
two('C15','100nF','VBUS','GND','power',(26.8,6.9))
two('C16','100nF','BST','SW_BUCK','power',(30.1,9))
two('L3','4.7uH Isat4.6A','SW_BUCK','+3V3','power',(28.8,12.5),'Inductor_4x4',mpn='XAL4030-472MEC')
two('C17','22uF 10V X5R','+3V3','GND','power',(26,14),'0805')
two('C18','22uF 10V X5R','+3V3','GND','power',(28.5,15.5),'0805')

# Display: 3.0V panel, 3.3V-tolerant input buffers, 4-wire SPI, CS tied low.
add('U5','3-channel LCD buffer','VSSOP8',{1:'LCD_MOSI',2:'LCD_CLK_3V0',3:'LCD_DC',4:'GND',
    5:'LCD_DC_3V0',6:'LCD_SCLK',7:'LCD_MOSI_3V0',8:'+3V0_LCD'},
    'display',(20,31),mpn='SN74LVC3G17DCUR',note='Check DCU pin table before finalizing.')
add('U6','3.0V LCD LDO','SOT23-5',{1:'+3V3',2:'GND',3:'+3V3',4:None,5:'+3V0_LCD'},
    'display',(26,34),mpn='TLV70030DDCR')
two('C19','1uF','+3V3','GND','display',(28,34))
two('C20','1uF','+3V0_LCD','GND','display',(24,34))
two('C21','100nF','+3V0_LCD','GND','display',(20,33))
two('C22','1uF','+3V0_LCD','GND','display',(23.5,38))
two('R12','47k','LCD_SCLK','GND','display',(20,28.8))
two('R13','10k','+3V0_LCD','LCD_RESET_N','display',(28,37))
two('C23','1uF','LCD_RESET_N','GND','display',(28,39))
add('D2','Reset clamp','SOD323',{1:'EN',2:'LCD_RESET_N'},'display',(26,37),mpn='BAT54WS',
    note='Cathode to EN; anode to LCD_RESET_N. Verify library numbering.')
two('R14','33R 0.5W','VBUS','LCD_LEDA','display',(5.2,39),'1210',
    note='Backlight <=82mA at 5.5V and 2.8V Vf; nominal about 55mA at 5V/3.2V.')
add('J2','LCD top-contact FPC24 0.5mm','Molex_52435_24P',
    {1:'GND',2:'+3V0_LCD',3:'+3V0_LCD',4:'GND',5:'LCD_RESET_N',6:'LCD_MOSI_3V0',
     7:'GND',8:'LCD_DC_3V0',9:'LCD_CLK_3V0',10:'GND',11:'+3V0_LCD',12:None,
     13:None,14:None,15:None,16:None,17:'LCD_LEDA',18:'GND',19:'GND',20:'GND',
     21:None,22:None,23:None,24:None},'display',(17.15,39.4),mpn='52435-2471',
    note='Top contacts, 0.3mm FPC. Mating mouth points toward the lower board edge.')

# Original-style differential PDM reconstruction and NS4150B amplifier.
for side,base_y,nstart,cstart in [('P',24.2,15,24),('N',26.0,18,27)]:
    nets=[f'PDM_{side}',f'PDM_{side}_F1',f'PDM_{side}_F2',f'PDM_{side}_F3']
    for i in range(3):
        two(f'R{nstart+i}','1k',nets[i],nets[i+1],'speaker',(20.5+2.6*i,base_y))
        two(f'C{cstart+i}', ['4.7nF','2.2nF','1nF'][i],nets[i+1],'GND',
            'speaker',(21.3+2.6*i,base_y+0.8))
two('C30','100nF','PDM_P_F3','PA_AC_P','speaker',(29.3,24.3))
two('C31','100nF','PDM_N_F3','PA_AC_N','speaker',(29.3,26.1))
two('R21','33k','PA_AC_P','PA_INP','speaker',(31.4,24.3))
two('R22','33k','PA_AC_N','PA_INN','speaker',(31.4,26.1))
add('U3','NS4150B','MSOP8',{1:'PA_EN',2:'PA_BYPASS',3:'PA_INP',4:'PA_INN',
    5:'SPK_N',6:'+3V3',7:'GND',8:'SPK_P'},'speaker',(29.5,29.4),mpn='NS4150B')
two('C32','1uF','PA_BYPASS','GND','speaker',(26.8,28.5))
two('C33','100nF','+3V3','GND','speaker',(32,29.2))
two('C34','10uF 10V X5R','+3V3','GND','speaker',(31.7,32),'0603')
add('J4','Speaker 8R >=1W','JST_SH_2_RA',{1:'SPK_P',2:'SPK_N'},'speaker',(30.8,42),
    mpn='SM02B-SRSS-TB(LF)(SN)',note='Floating BTL output: neither terminal is ground. Limit firmware volume.')

# Electret microphone and LMV321 inverting amplifier, original ADC DC offset.
two('R23','200R','+3V3','MIC_3V3','mic',(8.6,23))
two('C35','10uF 10V X5R','MIC_3V3','GND','mic',(6.5,23),'0603')
two('C36','100nF','+3V3','GND','mic',(11,28.5))
add('J3','Electret microphone','JST_SH_2_RA',{1:'MIC_P',2:'GND'},'mic',(3.5,29),mpn='SM02B-SRSS-TB(LF)(SN)')
two('R24','2.2k','MIC_3V3','MIC_P','mic',(5.5,25.4))
two('C37','22pF C0G','MIC_P','GND','mic',(5.5,27))
two('C38','4.7uF 10V X5R','MIC_P','MIC_AC','mic',(6,30.5),'0603')
two('R25','2k','MIC_AC','MIC_INV','mic',(8,30.5))
add('U4','LMV321IDBVR','SOT23-5',{1:'MIC_BIAS',2:'GND',3:'MIC_INV',4:'MIC_OUT',5:'+3V3'},
    'mic',(10.8,31),mpn='LMV321IDBVR')
two('R26','150k','MIC_INV','MIC_OUT','mic',(9.8,33.5))
two('C39','15pF C0G','MIC_INV','MIC_OUT','mic',(9.8,35.2))
two('R27','100k','+3V3','MIC_BIAS','mic',(11,26))
two('R28','100k','MIC_BIAS','GND','mic',(8.7,26))
two('C40','100nF','MIC_BIAS','GND','mic',(8.7,28))
two('C41','4.7uF 10V X5R','MIC_OUT','MIC_ADC','mic',(13.5,30.3),'0603')
two('R29','56k','+3V3','MIC_ADC','mic',(14.5,28))
two('R30','10k','MIC_ADC','GND','mic',(16.5,28))

# V6 supports >=3.3V supply and VIH=0.55*VDD. Direct MCU drive on the same rail.
two('R31','100R','RGB_3V3','RGB_D1','io',(4.5,8))
for i,xy in enumerate([(3,3),(7.8,3),(26.5,3),(31.3,3)],1):
    # Manufacturer V6 datasheet, page 2; the 5050 package has a different pinout.
    add(f'LED{i}','WS2812B-2020-V6','LED_2020_4P',{1:f'RGB_D{i+1}' if i<4 else None,
        2:'GND',3:f'RGB_D{i}',4:'+3V3'},'io',xy,mpn='WS2812B-2020-V6',
        note='V6 pinout: 1 DO, 2 GND, 3 DI, 4 VDD. Do not substitute other revisions without review.')
    two(f'C{42+i}','100nF','+3V3','GND','io',(xy[0],xy[1]+2.6))
add('J5','Wake expansion','JST_SH_4_RA',{1:'WAKE_MOVE',2:'WAKE_AUDIO',3:'VBUS',4:'GND'},
    'io',(3.8,35.5),mpn='SM04B-SRSS-TB(LF)(SN)',note='Signal pins are 3.3V only; matches original logical pin order.')
for i,(net,xy) in enumerate([('GND',(3,12)),('+3V3',(24,19)),('VBUS',(25,7)),
    ('UART_TX',(24,21)),('UART_RX',(26,21)),('EN',(10,21))],1):
    add(f'TP{i}',net,'TestPad_1mm',{1:net},'io',xy)

# The regulated nominal voltage is 3.392V (3.4V name); preserve GPIO identities.
for p in parts:
    p['pins'] = {k: {'+3V3':'+3V4','RF_3V3':'RF_3V4','RGB_3V3':'RGB_3V4',
                     'MIC_3V3':'MIC_3V4'}.get(v,v) for k,v in p['pins'].items()}
    if p['ref']=='TP2':p['value']='+3V4'
    if p['ref'].startswith('SW'):
        p['mpn']='PTS810 SJM 250 SMTR LFS'
        p['note']='Top-actuated C&K PTS810; duplicated pads 1 and 2 per library.'
# The USB connector has four combined VBUS/GND solder lands, not 16 independent lands.
usb=next(p for p in parts if p['ref']=='J1')
usb['pins']={'A1/B12':'GND','A4/B9':'VBUS','B8':None,'A5':'CC1',
             'B7':'USB_DM','A6':'USB_DP','A7':'USB_DM','B6':'USB_DP','A8':None,
             'B5':'CC2','A9/B4':'VBUS','A12/B1':'GND',
             'S1':'GND','S2':'GND','S3':'GND','S4':'GND'}
# Screen rear view: folded tail pin 1 is on the right. Connector contact 1 is
# on the left with its mouth pointing down. Keep both numbering systems explicit.
lcd=next(p for p in parts if p['ref']=='J2')
lcd['lcd_pin_to_connector_pin']={str(i):str(25-i) for i in range(1,25)}
lcd['pins']={str(25-int(k)):v for k,v in lcd['pins'].items()}
lcd['pins']['MP']='GND'
lcd['note']='Component-face view, mouth toward board bottom. LCD pin n -> J2 pin 25-n. FPC wrap and polarity require physical fit check.'
for p in parts:
    if p['ref'] in ('J3','J4','J5'):p['pins']['MP']='GND'
assert len({p['ref'] for p in parts}) == len(parts)
model = {'name':'ESP-HI-C3-Chip-RevA', 'status':'ELECTRICAL_DRAFT_UNVERIFIED',
         'board_mm':[34.3,46.35], 'thickness_mm':1.0, 'copper_layers':2,
         'assembly_side':'top', 'screen':'NHD-1.8-128160EF-SSXN-F',
         'screen_mm':[34.7,46.75], 'components':parts,
         'unresolved':['EDA library exact pin/footprint verification',
                       'RF matching and crystal load need prototype tuning',
                       'USB input current budget requires new firmware profile and prototype measurement',
                       'Native ERC/DRC and all-net routing not yet run']}
(ROOT/'design-intent.json').write_text(json.dumps(model,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
with (ROOT/'connection-intent.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['reference','pin','net','block'])
    for p in parts:
        for n,net in p['pins'].items():w.writerow([p['ref'],n,net or 'NC',p['block']])
with (ROOT/'BOM-draft.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['reference','value','mpn','footprint_intent','fitted','note'])
    for p in parts:w.writerow([p[k] for k in ['ref','value','mpn','footprint','fitted','note']])
print(f'Wrote {len(parts)} components; {sum(len(p["pins"]) for p in parts)} pin intents. Not an ERC/DRC pass.')
