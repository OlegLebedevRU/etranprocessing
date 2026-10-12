import os

# General API fixtures do not provision a private issuing key. CA hierarchy tests
# explicitly select local signing and provide isolated encrypted fixture keys.
os.environ.setdefault("CA_SIGNER_BACKEND", "external")

# Set required env vars before any app imports
os.environ.setdefault("SIGN_KEY", "test-sign-key")
os.environ.setdefault(
    "DATABASE_URL", "postgresql+asyncpg://test:test@localhost:5432/test"
)
os.environ.setdefault("CA_URL", "https://ca.test/sign-csr")
