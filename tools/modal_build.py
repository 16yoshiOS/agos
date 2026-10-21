#!/usr/bin/env python3
"""Build AGOS and its bootable ISO inside a Modal (modal.com) container.

Usage (from the repository root, with Modal set up: `pip install modal`,
`modal setup`):

    modal run tools/modal_build.py              # -> dist/agos.iso, dist/kernel.elf
    modal run tools/modal_build.py --out build-out

The container is Debian-based and gets exactly the packages the Makefile needs:
  nasm gcc binutils make python3          kernel build + Multiboot header check
  grub-pc-bin grub-common xorriso mtools  grub-mkrescue (BIOS-bootable ISO)

Steps inside the container: host unit/simulation tests, kernel build with
Multiboot verification, then `make iso`. The finished files are returned to
your machine; nothing is left running on Modal.
"""
import pathlib
import modal

REPO = pathlib.Path(__file__).resolve().parent.parent

image = (
    modal.Image.debian_slim()
    .apt_install(
        "nasm", "gcc", "binutils", "make", "python3",
        "grub-pc-bin", "grub-common", "xorriso", "mtools",
    )
    .add_local_dir(
        str(REPO),
        remote_path="/src",
        ignore=["build/**", ".git/**", "*.iso", "*.tar.gz", "__pycache__/**"],
    )
)

app = modal.App("agos-build", image=image)


@app.function(timeout=900)
def build_iso() -> dict:
    """Run the full AGOS pipeline and return the artifacts as bytes."""
    import shutil
    import subprocess

    work = pathlib.Path("/tmp/agos")           # build outside the mounted source
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree("/src", work)

    def run(*cmd):
        print("+", " ".join(cmd), flush=True)
        subprocess.run(cmd, cwd=work, check=True)

    run("make", "clean")
    run("make", "test")        # pure-logic + simulated-hardware tests
    run("make", "iso")         # builds kernel.elf, verifies Multiboot, runs grub-mkrescue

    return {
        "agos.iso": (work / "build" / "agos.iso").read_bytes(),
        "kernel.elf": (work / "build" / "kernel.elf").read_bytes(),
    }


@app.local_entrypoint()
def main(out: str = "dist"):
    files = build_iso.remote()
    dest = pathlib.Path(out)
    dest.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (dest / name).write_bytes(data)
        print(f"wrote {dest / name} ({len(data)} bytes)")
    print(f"\nBoot it:  qemu-system-i386 -cdrom {dest / 'agos.iso'} -m 128 -serial stdio")
