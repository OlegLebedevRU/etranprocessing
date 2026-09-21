import subprocess
import time
import os

l4c = r"C:\l4tools\l4capture\bin\l4capture.exe"
log = r"C:\l4tools\l4capture\test_stderr.log"

with open(log, "w") as f:
    f.write("=== l4capture manual test ===\n")

proc = subprocess.Popen(
    [l4c, "--pipe-in=300", "--pipe-out=301"],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    creationflags=0x08000000  # CREATE_NO_WINDOW
)

try:
    stdout, stderr = proc.communicate(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    stdout, stderr = proc.communicate()

print(f"Exit code: {proc.returncode}")
print(f"STDOUT: {stdout.decode('utf-8', errors='replace')[:500]}")
print(f"STDERR: {stderr.decode('utf-8', errors='replace')[:500]}")
