from pylib import *
from runner import *

import os
import sys
import time

from datetime import datetime

DIR = "./test/benchmarks/sil"
RESULT_DIR = "./benchmarks"
RESULT_FILE = "benchmark"
FORMAT = "{}\npytime:\n{}\n\nstdout:\n{}"
EXTS = {"sil"}
OPTIMIZATION = 2
SWITCHED_GOTO = True
NANBOXING = True
DUK = False


@runner(
    "__benchmark_build",
    nanboxing=NANBOXING,
    switched_goto=SWITCHED_GOTO,
    optimization=OPTIMIZATION,
)
def main(run: RunFunc):
    os.makedirs(RESULT_DIR, exist_ok=True)

    name = (
        f"{RESULT_DIR}/{RESULT_FILE}"
        + "_"
        + f"{datetime.now().strftime('%d-%m-%Y_%H-%M')}"
    )

    with open(name + ".txt", "w", encoding="utf-8") as file:
        for script in files(DIR, EXTS):
            print(script, "-> ...", end="", flush=True)

            start = time.perf_counter()
            out, err = run(str(script), None)
            perf = time.perf_counter() - start

            if err:
                print(f"\b\b\berror at {script}: {trim(err, 32)}")
                continue

            result = FORMAT.format(cover(str(script), 80), perf, out)
            file.write(result)
            file.flush()

            if DUK:
                parts = [*script.with_suffix(".js").parts]
                parts[-2] = "es5"
                script = Path(*parts)

                out, err = duk(str(script), None)
                if err:
                    print(f"duk: error at {script}: {trim(err, 32)}")
                else:
                    file.write(f"\nduk:\n{out}")
                    file.flush()

            print("\b\b\bdone!")

    print("all done!")


if __name__ == "__main__":
    if "duk" in sys.argv:
        DUK = True

    main()
