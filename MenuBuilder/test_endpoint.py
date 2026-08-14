import subprocess
import sys

SSH_BASE = [
    'ssh', '-i', r'd:\.ssh\free-tier-cloud_ru',
    '-o', 'ConnectTimeout=30',
    '-o', 'ServerAliveInterval=5',
    '-o', 'StrictHostKeyChecking=no',
    '-o', 'BatchMode=yes',
    'user1@176.108.247.249'
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
