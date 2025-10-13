# requires RPi_I2C_driver.py
import RPi_I2C_driver
from time import *


from datetime import datetime, timezone

import os
import glob
import time

# These tow lines mount the device:
os.system('modprobe w1-gpio')
os.system('modprobe w1-therm')
 
base_dir = '/sys/bus/w1/devices/'
# Get all the filenames begin with 28 in the path base_dir.

sensors = [
    "28-0730d44661a3",
    "28-0fe30087603c",
    "28-3ce1d443e26c"
]

device_folders = list(map(lambda s: base_dir + s, sensors))

devices_files = list(map(lambda s: s + '/w1_slave', device_folders))

mylcd = RPi_I2C_driver.lcd()
mylcd.backlight(0)
mylcd.lcd_display_string("Starting..", 1)
mylcd.lcd_display_string("¯\_(ツ)_/¯", 2)

sleep(2) # 2 sec delay

mylcd.lcd_clear()
mylcd.lcd_display_string("Reading:", 1)


def read_rom():
    name_file=device_folder+'/name'
    f = open(name_file,'r')
    return f.readline()
 
def read_temp_raw(index):
    f = open(devices_files[index], 'r')
    lines = f.readlines()
    f.close()
    return lines
 
def read_temp(index):
    lines = read_temp_raw(index)
    # Analyze if the last 3 characters are 'YES'.
    while lines[0].strip()[-3:] != 'YES':
        time.sleep(0.2)
        lines = read_temp_raw(index)
    # Find the index of 't=' in a string.
    equals_pos = lines[1].find('t=')
    if equals_pos != -1:
        # Read the temperature .
        temp_string = lines[1][equals_pos+2:]
        temp_c = float(temp_string) / 1000.0
        temp_f = temp_c * 9.0 / 5.0 + 32.0
        return temp_c, temp_f
 
# print(' rom: '+ read_rom())
while True:

    c_a,f_a = read_temp(0);
    c_b,f_b = read_temp(1);
    c_c,f_c = read_temp(2);

    now = datetime.now(timezone.utc)
    formated_time = now.strftime("%H:%M:%S")
    print(formated_time)

    template_a = "A={:2.2f}"
    result_a = template_a.format(c_a)
    template_b = "B={:2.2f}"
    result_b = template_b.format(c_b)
    template_c = "C={:2.2f}"
    result_c = template_c.format(c_c)

    print(result_a)

    mylcd.lcd_display_string(formated_time + ' ' + result_a, 1)
    mylcd.lcd_display_string(result_b + ' ' + result_c, 2)

    time.sleep(1)
time.sleep(1)