import subprocess
import sys

SSH_BASE = [
    'ssh', '-n', '-i', r'd:\.ssh\id_ed25519',
    '-o', 'ConnectTimeout=30',
    '-o', 'ServerAliveInterval=5',
    '-o', 'StrictHostKeyChecking=no',
    '-o', 'BatchMode=yes',
    'user1@87.242.100.34'
]

cmd = ' && '.join([
    'sudo systemctl restart menubuilder-backend',
    'sleep 2',
    'curl -s http://localhost:8000/api/ListMenuFile'
])

result = subprocess.run(SSH_BASE + [cmd], capture_output=True, text=True, timeout=45)
print(result.stdout)
if result.stderr:
    print(result.stderr, file=sys.stderr)
