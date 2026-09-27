#!/usr/bin/env bash
# Run on the approved build host from an extracted, verified Git archive.
set -euo pipefail
umask 077
: "${REGISTRY_PREFIX:?Set private registry namespace (without trailing slash)}"
: "${SOURCE_REVISION:?Set full Git commit}"
: "${SOURCE_ARCHIVE_SHA256:?Set verified Git archive SHA-256}"
[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]]
[[ "$SOURCE_ARCHIVE_SHA256" =~ ^[0-9a-f]{64}$ ]]
[[ "$REGISTRY_PREFIX" =~ ^[a-zA-Z0-9][a-zA-Z0-9./:_-]+$ ]]
root=$(cd "$(dirname "$0")/.." && pwd)
tag="git-${SOURCE_REVISION}"
ingress="${REGISTRY_PREFIX}/l4media-ingress:${tag}"
janus="${REGISTRY_PREFIX}/l4media-janus:${tag}"
for component in ingress janus; do
    sudo docker build --build-arg "SOURCE_REVISION=$SOURCE_REVISION" \
        --build-arg "SOURCE_ARCHIVE_SHA256=$SOURCE_ARCHIVE_SHA256" \
        -t "${REGISTRY_PREFIX}/l4media-${component}:${tag}" "$root/$component"
done
bash "$root/deploy/smoke-images.sh" "$ingress" "$janus"
for image in "$ingress" "$janus"; do
    python3 "$root/deploy/scan-image.py" "$image"
done
# Existing host credential store supplies registry auth. Never pass secrets as args.
for image in "$ingress" "$janus"; do
    sudo docker push "$image"
    sudo docker image inspect "$image" --format '{{json .RepoDigests}}'
done
