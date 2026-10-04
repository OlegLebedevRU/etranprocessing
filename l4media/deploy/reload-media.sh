#!/bin/sh
# Run on the media host from any working directory, after certificate renewal.
set -eu
media_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$media_dir"
# A fresh container sees renewed bind-mount files even after atomic replacement.
sudo docker compose -p l4media run --rm --no-deps \
    --entrypoint /docker-entrypoint.d/40-verify-media-certificate.sh nginx
sudo docker compose -p l4media up -d --no-deps --force-recreate nginx
sudo docker compose -p l4media exec -T nginx nginx -t
