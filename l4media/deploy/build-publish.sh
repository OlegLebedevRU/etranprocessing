#!/usr/bin/env bash
# Run on the build host or local fallback, from an extracted, verified Git archive.
set -euo pipefail
umask 077
: "${REGISTRY_PREFIX:?Set private registry namespace (without trailing slash)}"
: "${SOURCE_REVISION:?Set full Git commit}"
: "${SOURCE_ARCHIVE_SHA256:?Set verified Git archive SHA-256}"
[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]]
[[ "$SOURCE_ARCHIVE_SHA256" =~ ^[0-9a-f]{64}$ ]]
[[ "$REGISTRY_PREFIX" =~ ^[a-zA-Z0-9][a-zA-Z0-9./:_-]+$ ]]
root=$(cd "$(dirname "$0")/.." && pwd)
read -r -a docker_cmd <<< "${DOCKER_COMMAND:-sudo docker}"
tag="git-${SOURCE_REVISION}"
ingress="${REGISTRY_PREFIX}/l4media-ingress:${tag}"
components=(ingress)
case "${1:-}" in
    '')
        janus_digest=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["janus"]["digest"])' "$root/deploy/baseline-images.json")
        # The accepted Janus baseline is at the registry root, while ingress
        # releases may use a namespace. Keep the baseline digest immutable.
        janus="${JANUS_BASELINE_IMAGE:-${REGISTRY_PREFIX%%/*}/l4media-janus@${janus_digest}}"
        [[ "$janus" == *@"$janus_digest" ]] || { echo 'Janus baseline digest mismatch' >&2; exit 2; }
        "${docker_cmd[@]}" pull "$janus"
        ;;
    --build-janus)
        # Only after a separate explicit user command to rebuild Janus.
        components+=(janus)
        janus="${REGISTRY_PREFIX}/l4media-janus:${tag}"
        ;;
    *) echo 'Usage: build-publish.sh [--build-janus]' >&2; exit 2 ;;
esac
for component in "${components[@]}"; do
    "${docker_cmd[@]}" build --build-arg "SOURCE_REVISION=$SOURCE_REVISION" \
        --build-arg "SOURCE_ARCHIVE_SHA256=$SOURCE_ARCHIVE_SHA256" \
        -t "${REGISTRY_PREFIX}/l4media-${component}:${tag}" "$root/$component"
done
bash "$root/deploy/smoke-images.sh" "$ingress" "$janus"
for image in "$ingress" "$janus"; do
    python3 "$root/deploy/scan-image.py" "$image"
done
# Existing host credential store supplies registry auth. Never pass secrets as args.
for component in "${components[@]}"; do
    image="${REGISTRY_PREFIX}/l4media-${component}:${tag}"
    "${docker_cmd[@]}" push "$image"
    "${docker_cmd[@]}" image inspect "$image" --format '{{json .RepoDigests}}'
done
