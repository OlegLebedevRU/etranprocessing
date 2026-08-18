"""Logging configuration with rotation."""

import logging
import os
from logging.handlers import RotatingFileHandler

LOG_DIR = os.environ.get("LOG_DIR", "/app/log")
LOG_FILE = "payment.log"
MAX_BYTES = 10 * 1024 * 1024  # 10 MB
BACKUP_COUNT = 5

_initialized = False


def _ensure_log_dir() -> None:
    """Create log directory if it doesn't exist. Safe for local dev."""
    global _initialized
    if _initialized:
        return
    try:
        os.makedirs(LOG_DIR, exist_ok=True)
        _initialized = True
    except OSError:
        # Fallback: disable file logging if dir can't be created
        pass


def setup_logger(name: str, filename: str) -> logging.Logger:
    """Setup a named logger with rotation."""
    _ensure_log_dir()

    logger = logging.getLogger(name)
    logger.setLevel(logging.INFO)

    log_path = os.path.join(LOG_DIR, filename)
    try:
        handler = RotatingFileHandler(
            log_path,
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
    except OSError:
        # If file logging fails, log to stderr as fallback
        handler = logging.StreamHandler()
        formatter = logging.Formatter(
            "%(asctime)s - %(name)s - %(levelname)s - %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
        )
        handler.setFormatter(formatter)
        logger.addHandler(handler)

    return logger


# Global logger instances
payment_logger = setup_logger("payment", "payment.log")
cert_logger = setup_logger("certificates", "certificates.log")
