FROM nginx:stable-alpine

# Remove default http configurations and create directories
RUN rm -rf /etc/nginx/conf.d/* && mkdir -p /etc/nginx/stream.d /crt

# Use custom nginx.conf with stream block
COPY nginx.conf /etc/nginx/nginx.conf
COPY stream.conf /etc/nginx/stream.d/stream.conf

EXPOSE 8443

CMD ["nginx", "-g", "daemon off;"]
