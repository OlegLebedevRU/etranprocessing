#!/bin/sh
# Write JWT key to file (hex string for HS256)
printf '%s' "${JWT_SECRET_HEX}" > /etc/nginx/jwt_key.hex

# Substitute JWT_SECRET_HEX and INTERNAL_SERVICE_KEY in nginx config template
envsubst '${JWT_SECRET_HEX} ${INTERNAL_SERVICE_KEY}' < /etc/nginx/conf.d/default.conf.template > /etc/nginx/conf.d/default.conf

# Start nginx directly
exec nginx
