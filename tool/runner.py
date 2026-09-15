import os
import subprocess

from typing import Callable
from functools import wraps

WD = "."


type RunFunc = Callable[[str, int | None], tuple[str, str]]


def runner(
    name: str,
    syntax: bool = False,
    sanitize: bool = False,
    nanboxing: bool = False,
    switched_goto: bool = False,
    optimization: int = 0,
):
    def deco(func: Callable[[RunFunc], None]):
        @wraps(func)
        def inner():
            cwd = os.getcwd()
            os.chdir(WD)

            cmd = ["make build", f"appname={name}"]

            if sanitize:
                cmd.append("sanitize=1")

            if syntax:
                cmd.append("syntax=1")

            if switched_goto:
                cmd.append("switched=1")

            if nanboxing:
                cmd.append("nanbox=1")

            cmd.append(f"o={max(min(optimization, 3), 0)}")

            os.system(" ".join(cmd))
            try:

                def run_script(
                    script: str, timeout: int | None = None
                ) -> tuple[str, str]:
                    result = subprocess.run(
                        [
                            "make",
                            "execute",
                            f"appname={name}",
                            f"script={script}",
                        ],
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                        timeout=timeout,
                    )
                    return result.stdout, result.stderr

                func(run_script)
            finally:
                os.system(f"make destroy appname={name}")
                os.chdir(cwd)

        return inner

    return deco


def do(name: str, script: str, timeout: int | None = None):
    result = subprocess.run(
        [name, script],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=timeout,
    )
    return result.stdout, result.stderr
