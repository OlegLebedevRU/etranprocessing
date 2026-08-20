import os

os.environ.setdefault("JWT_SECRET", "test-secret-key-12345678901234567890")
os.environ.setdefault(
    "JWT_SECRET_HEX",
    "176b79312cfae3cf5e6c0200548f32c7c47e7b06b933eb11c9b2936bfe2644bc",
)
os.environ.setdefault(
    "AUTH_USERS",
    '[{"username":"o.lebedev","md5_password":"eaf21fcabcffeb1f97f01a4fc02ece63","org_id":1,"role":"superuser","is_superuser":true},{"username":"test","md5_password":"cc03e747a6afbbcbf8be7668acfebee5","org_id":1,"role":"user","is_superuser":false}]',
)
os.environ.setdefault(
    "DATABASE_URL", "postgresql+asyncpg://test:test@localhost:5432/test"
)
