import os

# Set required env vars before any app imports
os.environ.setdefault("SIGN_KEY", "test-sign-key")
os.environ.setdefault(
    "DATABASE_URL", "postgresql+asyncpg://test:test@localhost:5432/test"
)
os.environ.setdefault("CA_URL", "https://ca.test/sign-csr")
