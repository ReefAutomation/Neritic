import json
import time
import urllib.request


BASE = "http://192.168.0.59"


def get_homekit(timeout=3):
    with urllib.request.urlopen(BASE + "/api/homekit", timeout=timeout) as response:
        return json.loads(response.read().decode())


def post_command(command: str):
    request = urllib.request.Request(
        BASE + "/api/command",
        data=json.dumps({"command": command}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        return response.read().decode()


def main():
    print("reset_response", post_command("homekit_reset_data"))

    went_down = False
    running = False
    start = time.time()

    while time.time() - start < 120:
        try:
            homekit = get_homekit(timeout=2)
            if went_down and homekit.get("nativeScaffoldStatus") == "adapter-ready:running":
                running = True
                print("running_after_reset", int(time.time() - start), "sec")
                print("setupUri", homekit.get("setupUri"))
                print("pairedControllerCount", homekit.get("pairedControllerCount"))
                print("accessoryPaired", homekit.get("accessoryPaired"))
                break
        except Exception:
            went_down = True
        time.sleep(1)

    if not running:
        try:
            homekit = get_homekit(timeout=2)
            print("not_running_in_time status=", homekit.get("nativeScaffoldStatus"))
        except Exception as error:
            print("not_running_in_time error=", error)
        raise SystemExit(1)

    print("PAIR_NOW: Open Apple Home and add DeepGlow now.")

    last_count = None
    last_paired = None
    monitor_start = time.time()
    paired_detected = False

    while time.time() - monitor_start < 180:
        try:
            homekit = get_homekit(timeout=2)
            count = homekit.get("pairedControllerCount")
            paired = homekit.get("accessoryPaired")
            if count != last_count or paired != last_paired:
                print(
                    "state",
                    int(time.time() - monitor_start),
                    "sec",
                    "count=",
                    count,
                    "paired=",
                    paired,
                    "status=",
                    homekit.get("nativeScaffoldStatus"),
                )
                last_count, last_paired = count, paired
            if isinstance(count, int) and count > 0:
                paired_detected = True
                print("PAIRING_CONFIRMED count=", count)
                break
        except Exception as error:
            print("poll_error", error)
        time.sleep(2)

    if not paired_detected:
        print("PAIRING_NOT_DETECTED_IN_WINDOW")


if __name__ == "__main__":
    main()
