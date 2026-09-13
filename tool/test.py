from pylib import *
from runner import *

from os.path import join

DIR = "./test/language"
EXTS = {"sil"}


def run_tests(
    run: RunFunc,
    label: str = "",
    exts: set[str] = EXTS,
    tag: str = "",
    skip_tags: set[str] | None = None,
    directory: str = DIR,
):
    print(cover(label))

    errors = 0

    for script in files(directory, exts=exts, recursive=True):
        if tag and not has_tag(script, tag):
            continue

        skip_flag = False
        if skip_tags:
            for skip_tag in skip_tags:
                if has_tag(script, skip_tag):
                    print(f"{str(script):64} -> skip")
                    skip_flag = True
                    break
        if skip_flag:
            continue

        expect_out, expect_err = read_out(script), read_err(script)

        out, err = run(str(script), 2)

        result: str | None
        if err:
            if not expect_err:
                result = f"unexpected error '{trim(err, 120).strip()}'"
            else:
                result = check_err(err, expect_err)
        elif expect_err:
            result = f"expected error '{trim(expect_err, 120).strip()}'"
        else:
            result = check_out(out, expect_out)

        if not result:
            print(f"{str(script):64} -> ok")
        else:
            errors += 1
            print(f"{str(script):64} -> error:", trim(result, 120).strip())

    print(cover("result"))
    print("ok" if not errors else f"error [{errors}]")


@runner("__test_build_base", sanitize=True)
def run_base_tests(run: RunFunc):
    run_tests(run, "base", skip_tags={"skip", "syntax"})


@runner("__test_build_syntax", sanitize=True, syntax=True)
def run_syntax_tests(run: RunFunc):
    run_tests(
        run,
        "syntax",
        directory=join(DIR, "__syntax"),
        tag="syntax",
        skip_tags={"skip"},
    )


if __name__ == "__main__":
    run_base_tests()
    run_syntax_tests()
