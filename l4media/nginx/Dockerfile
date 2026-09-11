FROM nginx:stable-alpine@sha256:862dc06c359bfe5d3211e4106269f040d261e269e58ebf17060d8328c45067c0

# Remove default http configurations and create directories
RUN rm -rf /etc/nginx/conf.d/* && mkdir -p /etc/nginx/stream.d /crt

# Use custom nginx.conf with stream block
COPY nginx.conf /etc/nginx/nginx.conf
COPY stream.conf /etc/nginx/stream.d/stream.conf

EXPOSE 8443

CMD ["nginx", "-g", "daemon off;"]
