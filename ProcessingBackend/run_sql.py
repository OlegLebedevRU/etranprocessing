import subprocess

# Upload migration file
subprocess.run([
    'scp', '-i', r'd:\.ssh\id_ed25519',
    r'D:\repo\platerra\Public\etranprocessing\ProcessingBackend\backend\alembic\versions\003_add_menubuilder_tables.py',
    'user1@87.242.100.34:/home/user1/ProcessingBackend/backend/alembic/versions/'
], timeout=15)

# Stamp alembic to 003
cmd = [
    'ssh', '-n', '-o', 'ConnectTimeout=3',
    'user1@87.242.100.34', '-i', r'd:\.ssh\id_ed25519',
    "psql -h 10.0.0.7 -U etran_db_user -d etran -c \"UPDATE alembic_version SET version_num='003'\""
]
r = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
print(r.stdout)

# Verify
cmd2 = [
    'ssh', '-n', '-o', 'ConnectTimeout=3',
    'user1@87.242.100.34', '-i', r'd:\.ssh\id_ed25519',
    "psql -h 10.0.0.7 -U etran_db_user -d etran -c 'SELECT * FROM alembic_version'"
]
r2 = subprocess.run(cmd2, capture_output=True, text=True, timeout=15)
print('Alembic version:', r2.stdout.strip())
