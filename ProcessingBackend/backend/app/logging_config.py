"""Logging configuration with rotation."""

import logging
import os
from logging.handlers import RotatingFileHandler

LOG_DIR = "/app/log"
LOG_FILE = "payment.log"
MAX_BYTES = 10 * 1024 * 1024  # 10 MB
BACKUP_COUNT = 5


def setup_payment_logger() -> logging.Logger:
    """Setup payment logger with rotation."""
    os.makedirs(LOG_DIR, exist_ok=True)
    
    logger = logging.getLogger("payment")
    logger.setLevel(logging.INFO)
    
    # Rotating file handler
    handler = RotatingFileHandler(
        os.path.join(LOG_DIR, LOG_FILE),
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


# Global logger instance
payment_logger = setup_payment_logger()
