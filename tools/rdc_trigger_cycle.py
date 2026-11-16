# rehatched-nx (2026-11-16): tools/rdc_trigger.py for a program with more than one window or device (Ryujinx draws
# its own interface and the game through separate Vulkan surfaces; the active one is the interface). At each time of
# RDC_TIMES it cycles the active window RDC_CYCLES times (as F11 does) and takes a capture after each step, so one of
# them is the game's. Run inside qrenderdoc:
#   RDC_OUT=<folder> RDC_TIMES="110,113" RDC_CYCLES=3 qrenderdoc --python tools/rdc_trigger_cycle.py
import os
import shutil
import time
import traceback

import renderdoc as rd

OUT = os.environ.get("RDC_OUT", ".")
TIMES = [float(t) for t in os.environ.get("RDC_TIMES", "60").split(",") if t.strip()]
CYCLES = int(os.environ.get("RDC_CYCLES", "3"))
FRAMES = int(os.environ.get("RDC_FRAMES", "1"))
log = open(os.path.join(OUT, "trigger.txt"), "w")


def say(*parts):
    log.write(time.strftime("%H:%M:%S ") + " ".join(str(p) for p in parts) + "\n")
    log.flush()


def connect():
    for _ in range(600):
        i = rd.EnumerateRemoteTargets("", 0)
        while i:
            tc = rd.CreateTargetControl("", i, "rehatched-nx rdc_trigger_cycle", True)
            if tc:
                say("connected to", i, tc.GetTarget(), "pid", tc.GetPID())
                return tc
            i = rd.EnumerateRemoteTargets("", i)
        time.sleep(1)
    return None


def drain(tc, seconds, state):
    end = time.time() + seconds
    while time.time() < end:
        msg = tc.ReceiveMessage(None)
        if msg.type == rd.TargetControlMessageType.NewCapture:
            state["taken"] += 1
            src = msg.newCapture.path
            dest = os.path.join(OUT, "capture_%02d_frame%d.rdc" % (state["taken"], msg.newCapture.frameNumber))
            for _ in range(50):
                if os.path.exists(src):
                    break
                time.sleep(0.1)
            try:
                shutil.copyfile(src, dest)
                say("capture", state["taken"], "frame", msg.newCapture.frameNumber, "->", dest,
                    os.path.getsize(dest), "bytes")
            except Exception as e:
                say("copy failed", src, e)
        elif msg.type == rd.TargetControlMessageType.Disconnected:
            say("disconnected")
            return False
        time.sleep(0.05)
    return True


try:
    tc = connect()
    if not tc:
        say("no target")
    else:
        start = time.time()
        state = {"taken": 0}
        alive = True
        for t in sorted(TIMES):
            while alive and time.time() - start < t:
                alive = drain(tc, 0.5, state)
            if not alive:
                break
            for k in range(CYCLES):
                tc.CycleActiveWindow()
                say("cycled window", k + 1, "at", round(time.time() - start, 1), "s; trigger")
                tc.TriggerCapture(FRAMES)
                alive = drain(tc, 1.5 + FRAMES, state)
                if not alive:
                    break
        if alive:
            drain(tc, 5, state)
        tc.Shutdown()
        say("done,", state["taken"], "captures")
except Exception:
    say(traceback.format_exc())
log.close()
os._exit(0)
