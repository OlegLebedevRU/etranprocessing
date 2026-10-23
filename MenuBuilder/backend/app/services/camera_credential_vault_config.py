"""Runtime-only stable vault secret; independent of terminal/TLS certificates."""

import base64

from pydantic import Field, SecretStr
from pydantic_settings import BaseSettings, SettingsConfigDict


class CameraCredentialVaultSettings(BaseSettings):
    model_config = SettingsConfigDict(
        extra="ignore", hide_input_in_errors=True, env_prefix="CAMERA_CREDENTIAL_"
    )
    kek_b64: SecretStr = SecretStr("")
    kek_id: str = Field(default="", max_length=64, pattern=r"^[A-Za-z0-9._-]*$")

    def key(self) -> bytes:
        try:
            encoded = self.kek_b64.get_secret_value()
            value = base64.b64decode(encoded, validate=True)
            if (
                len(value) != 32
                or not self.kek_id
                or base64.b64encode(value).decode() != encoded
            ):
                raise ValueError
            return value
        except ValueError, UnicodeError:
            raise ValueError("camera_credential_vault_key_unavailable") from None
