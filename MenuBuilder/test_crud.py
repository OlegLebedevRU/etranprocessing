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

def run_ssh(cmd, timeout=30):
    result = subprocess.run(SSH_BASE + [cmd], capture_output=True, text=True, timeout=timeout)
    if result.stdout:
        print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='', file=sys.stderr)
    return result.returncode

if __name__ == '__main__':
    # Test create group
    cmd = """curl -s -X POST http://localhost:8000/api/groups -H 'Content-Type: application/json' -d '{"org_id":1,"name":"TestAPI","number":99,"parent_id":null}'"""
    print("CREATE GROUP:")
    run_ssh(cmd)
    print()
    
    # Test get groups
    print("\nGET GROUPS:")
    run_ssh("curl -s http://localhost:8000/api/groups")
    print()
    
    # Test delete the test group
    print("\nDELETE GROUP 5:")
    run_ssh("curl -s -X DELETE http://localhost:8000/api/groups/5")
    print()
