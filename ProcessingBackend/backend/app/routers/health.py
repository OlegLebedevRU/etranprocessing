import logging
from fastapi import APIRouter, Request

router = APIRouter()
logger = logging.getLogger(__name__)


@router.get("/health")
async def health():
    return {"status": "ok"}


@router.get("/debug/headers")
async def debug_headers(request: Request):
    headers = dict(request.headers)
    logger.info(f"Debug headers: {headers}")
    return headers
