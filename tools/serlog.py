#!/usr/bin/env python3
# Soros naplo olvasasa ujracsatlakozassal (az ESP32-S3 USB-CDC ujraindulaskor eltunik/visszajon).
# DTR/RTS nem aktiv, hogy a megnyitas ne resetelje/ne tegye letoltesi modba a lapkat.
# Hasznalat: python serlog.py /dev/cu.usbmodemXXXX [masodperc] [--reset]
#   --reset: normal (nem letoltesi) ujrainditas az USB-Serial/JTAG RTS-vonalan, DTR=0 mellett (GPIO0 nem huzodik le).
import sys, time, serial

port = sys.argv[1]
dur = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 30
reset = "--reset" in sys.argv
end = time.time() + dur
while time.time() < end:
    try:
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.dtr, s.rts = port, 115200, 0.3, False, False
        s.open()
        print(f"--- [{time.strftime('%H:%M:%S')}] port nyitva", flush=True)
        if reset:
            reset = False
            s.dtr = False
            s.rts = True
            time.sleep(0.1)
            s.rts = False
            print("--- reset elkuldve", flush=True)
        while time.time() < end:
            b = s.read(4096)
            if b:
                sys.stdout.write(b.decode(errors="replace"))
                sys.stdout.flush()
    except (serial.SerialException, OSError) as e:
        print(f"\n--- [{time.strftime('%H:%M:%S')}] port elveszett: {type(e).__name__}", flush=True)
        time.sleep(0.05)
