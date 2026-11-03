import time
import csv
from collections import deque
from datetime import datetime, timezone, timedelta

# --- CONFIG ---
SAMPLE_INTERVAL_SEC = 60        # 1 minute; change to 300 for 5 min, etc.
ROLLING_N = 5                   # compute rolling average over last N samples
FAST_RISE_THRESHOLD = 2.0       # degC rise considered "fast" (over FAST_RISE_WINDOW)
FAST_RISE_WINDOW_MIN = 30       # minutes over which to check fast rise
CSV_FILE = 'fermentation_log.csv'
# ----------------

# placeholder: replace with actual sensor-reading implementation
def read_temp():
    # e.g., read from DS18B20, I2C sensor, ADC, etc.
    # return float temperature in Celsius
    raise NotImplementedError("Replace read_temp() with your sensor code")

def iso_now():
    # local timezone aware ISO8601 (adjust as needed)
    return datetime.now().astimezone().isoformat()

def append_csv(row):
    header = ['timestamp', 'temp_c', 'rolling_avg_c']
    try:
        with open(CSV_FILE, 'r'):
            pass
    except FileNotFoundError:
        with open(CSV_FILE, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(header)
    with open(CSV_FILE, 'a', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(row)

def alert(message):
    # implement alert: email, push, GPIO buzzer, telegram, etc.
    print("ALERT:", message)

def run_logger():
    window = deque(maxlen=ROLLING_N)
    recent_samples = deque()  # tuples (timestamp_seconds, temp)
    fast_window_sec = FAST_RISE_WINDOW_MIN * 60

    while True:
        try:
            temp = read_temp()
        except Exception as e:
            print("Sensor read error:", e)
            time.sleep(SAMPLE_INTERVAL_SEC)
            continue

        ts = time.time()
        window.append(temp)
        recent_samples.append((ts, temp))

        # remove old entries for fast-rise detection
        while recent_samples and (ts - recent_samples[0][0] > fast_window_sec):
            recent_samples.popleft()

        rolling_avg = sum(window) / len(window)

        # log
        append_csv([iso_now(), f"{temp:.2f}", f"{rolling_avg:.2f}"])
        print(f"{iso_now()}  temp={temp:.2f}C  avg={rolling_avg:.2f}C")

        # fast-rise detection: compare oldest in window to newest
        if len(recent_samples) >= 2:
            oldest_ts, oldest_temp = recent_samples[0]
            # temp change over FAST_RISE_WINDOW_MIN
            delta = temp - oldest_temp
            if delta >= FAST_RISE_THRESHOLD:
                alert(f"Fast temp rise: +{delta:.2f}°C in last {FAST_RISE_WINDOW_MIN} min")

        time.sleep(SAMPLE_INTERVAL_SEC)

if __name__ == "__main__":
    run_logger()