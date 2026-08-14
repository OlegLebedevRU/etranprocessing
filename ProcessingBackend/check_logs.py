import subprocess, sys, time

ssh_key = 'd:\\.ssh\\free-tier-cloud_ru'
host = 'user1@176.108.247.249'

# Wait a bit for terminal request
time.sleep(10)

# Check logs
r = subprocess.run(
    ['ssh', '-i', ssh_key, '-o', 'StrictHostKeyChecking=no', '-o', 'ConnectTimeout=30', host,
     'sudo docker logs processing-backend --tail 10 2>&1'],
    capture_output=True, text=True, timeout=30
)
print(r.stdout)
