"""Logging configuration with rotation."""

import logging
import os
from logging.handlers import RotatingFileHandler

LOG_DIR = "/app/log"
LOG_FILE = "payment.log"
MAX_BYTES = 10 * 1024 * 1024  # 10 MB
BACKUP_COUNT = 5


def setup_logger(name: str, filename: str) -> logging.Logger:
    """Setup a named logger with rotation."""
    os.makedirs(LOG_DIR, exist_ok=True)

    logger = logging.getLogger(name)
    logger.setLevel(logging.INFO)

    handler = RotatingFileHandler(
        os.path.join(LOG_DIR, filename),
        maxBytes=MAX_BYTES,
        backupCount=BACKUP_COUNT,
        encoding="utf-8",
    )

    formatter = logging.Formatter(
        "%(asctime)s - %(levelname)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    handler.setFormatter(formatter)
    logger.addHandler(handler)

    return logger


# Global logger instances
payment_logger = setup_logger("payment", "payment.log")
cert_logger = setup_logger("certificates", "certificates.log")
