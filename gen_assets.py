#!/usr/bin/env python3

import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from app_assets import Assets

a = Assets("HF")

a.text("T_HF",      "HF")
a.text("T_SAVE",    "SAVE?")
a.text("T_DEL",     "DEL?")
a.text("T_SCAN",    "SCAN")
a.text("T_BAND",    "BAND")
a.text("T_MEMORY",  "MEM")
a.text("T_FREQ",    "FREQ")
a.text("T_STEP",    "STEP")
a.text("T_SNR",     "SNR")
a.text("T_RSSI",    "RSSI")

a.text("T_AM",      "AM")
a.text("T_LSB",     "LSB")
a.text("T_USB",     "USB")
a.text("T_CW",      "CW")

a.text("T_SW",      "SW")
a.text("T_MW",      "MW")
a.text("T_LW",      "LW")

a.table(
    "BAND_NAME",
    [
        "LW",
        "MW",
        "160M",
        "80M",
        "40M",
        "20M",
        "15M",
        "10M"
    ]
)

a.u8(
    "BMP_HF",
    [
        0x3e,0x41,0x41,0x7f,
        0x41,0x41,0x41,0x00
    ]
)

a.main()
