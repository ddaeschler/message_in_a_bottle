import gzip
import pathlib
import shutil

Import("env")

PROJECT_DIR = pathlib.Path(env.subst("$PROJECT_DIR"))
SOURCE_DIR = PROJECT_DIR / "www"
DATA_DIR = PROJECT_DIR / "data"

COMPRESSIBLE_SUFFIXES = {
    ".html",
    ".css",
    ".js",
    ".json",
    ".svg",
    ".txt",
}


def gzip_assets(source, target, env):
    del source, target, env

    DATA_DIR.mkdir(parents=True, exist_ok=True)

    for input_path in SOURCE_DIR.rglob("*"):
        if not input_path.is_file():
            continue

        relative_path = input_path.relative_to(SOURCE_DIR)
        output_path = DATA_DIR / relative_path

        output_path.parent.mkdir(parents=True, exist_ok=True)

        if input_path.suffix.lower() in COMPRESSIBLE_SUFFIXES:
            gzip_path = pathlib.Path(str(output_path) + ".gz")

            with input_path.open("rb") as source_file:
                with gzip.open(
                    gzip_path,
                    "wb",
                    compresslevel=9,
                ) as compressed_file:
                    shutil.copyfileobj(source_file, compressed_file)

            print(f"Gzip: {relative_path} -> {gzip_path.relative_to(DATA_DIR)}")
        else:
            shutil.copy2(input_path, output_path)
            print(f"Copy: {relative_path}")


env.AddPreAction("buildfs", gzip_assets)
env.AddPreAction("uploadfs", gzip_assets)