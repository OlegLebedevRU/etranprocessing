import subprocess

# Upload migration file
subprocess.run([
    'scp', '-i', r'd:\.ssh\free-tier-cloud_ru',
    r'D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\alembic\versions\003_add_menubuilder_tables.py',
    'user1@176.108.247.249:/home/user1/ProcessingBackend/backend/alembic/versions/'
], timeout=15)

# Stamp alembic to 003
cmd = [
    'ssh', '-o', 'ConnectTimeout=3',
    'user1@176.108.247.249', '-i', r'd:\.ssh\free-tier-cloud_ru',
    "sudo docker exec iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing -c \"UPDATE alembic_version SET version_num='003'\""
]
r = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
print(r.stdout)

# Verify
cmd2 = [
    'ssh', '-o', 'ConnectTimeout=3',
    'user1@176.108.247.249', '-i', r'd:\.ssh\free-tier-cloud_ru',
    "sudo docker exec iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing -c 'SELECT * FROM alembic_version'"
]
r2 = subprocess.run(cmd2, capture_output=True, text=True, timeout=15)
print('Alembic version:', r2.stdout.strip())
