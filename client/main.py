# requires RPi_I2C_driver.py
from zoneinfo import ZoneInfo
import RPi_I2C_driver
from time import *
from datetime import datetime, timezone, timedelta
import requests



import os
import glob
import time

# These tow lines mount the device:
os.system('modprobe w1-gpio')
os.system('modprobe w1-therm')
 
base_dir = '/sys/bus/w1/devices/'
# Get all the filenames begin with 28 in the path base_dir.

# motmot 1
sensors = [
    "28-0730d44661a3",
    "28-0fe30087603c",
    "28-3ce1d443e26c"
]

id = 'gaspar'

# motmot 2
sensors = [
    "28-00000077e190",
    "28-000000b92ec0",
    "28-000000b94557"
]

device_folders = list(map(lambda s: base_dir + s, sensors))

devices_files = list(map(lambda s: s + '/w1_slave', device_folders))

mylcd = RPi_I2C_driver.lcd()
mylcd.backlight(0)
mylcd.lcd_display_string("Starting..", 1)
mylcd.lcd_display_string("(ツ)", 2)

sleep(2) # 2 sec delay

mylcd.lcd_clear()
mylcd.lcd_display_string("Reading:", 1)


# def read_rom():
#     name_file=device_folder+'/name'
#     f = open(name_file,'r')
#     return f.readline()
 
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

def send_data(file_path):
    try:
        # Read the file content
        with open(file_path, 'r', encoding='utf-8') as file:
            content = file.read()

        # Send content to the endpoint
        response = requests.post(endpoint, data=content)

        # Check response
        if response.status_code == 200:
            print("Successfully sent file content to endpoint")
            print("Response:", response.text)
            return 'ok'
        else:
            print(f"Failed to send data. Status code: {response.status_code}")
            print("Response:", response.text)
            return str(response.status_code)

    except FileNotFoundError:
        print(f"File not found: {file_path}")
        return 'NotFound'
    except requests.exceptions.RequestException as e:
        print(f"Error sending request to {endpoint}: {e}")
        return f"{e}"
 
# print(' rom: '+ read_rom())
last_sample_sent_time = datetime.now(timezone.utc) - timedelta(minutes=5)
endpoint = "http://18.221.254.212:8080/samples"
while True:

    c_a,f_a = read_temp(0);
    c_b,f_b = read_temp(1);
    c_c,f_c = read_temp(2);

    now = datetime.now(timezone.utc)


    gmt_minus_6_offset = timezone(timedelta(hours=-6))
    gmt_minus_6_time = now.astimezone(gmt_minus_6_offset)

    formated_time = gmt_minus_6_time.strftime("%H:%M:%S")
    # print(formated_time)

    template_a = "A={:2.2f}"
    result_a = template_a.format(c_a)
    template_b = "B={:2.2f}"
    result_b = template_b.format(c_b)
    template_c = "C={:2.2f}"
    result_c = template_c.format(c_c)

    # print(result_a)


    mylcd.lcd_display_string(formated_time + ' ' + result_a, 1)
    mylcd.lcd_display_string(result_b + ' ' + result_c, 2)

    # Calculate the time difference
    time_diff = now - last_sample_sent_time
    if time_diff.seconds > 5 * 60:
        last_sample_sent_time = now

        date_in_iso_format = gmt_minus_6_time.isoformat()
        date_only = date_in_iso_format.split("T")[0]

        clean_temp_template = "{:2.2f}"
        record = id+','+date_in_iso_format+','+ clean_temp_template.format(c_a) + ',' + clean_temp_template.format(c_b) + ',' + clean_temp_template.format(c_c) + '\n'

        print(record)

        # Open file in append mode
        with open('data/'+date_only, 'a') as file:
            file.write(record)

        result = send_data('data/'+date_only)

        mylcd.lcd_clear()
        if result == 'ok':
            mylcd.lcd_display_string("Data sent", 1)
            mylcd.lcd_display_string(date_in_iso_format, 2)
        else:
            mylcd.lcd_display_string(result, 1)
            mylcd.lcd_display_string(date_in_iso_format, 2)
        time.sleep(1)

    time.sleep(1)