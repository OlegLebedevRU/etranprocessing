FROM node:24-alpine AS build
WORKDIR /app
COPY build.mjs package.json ./
COPY src ./src
COPY assets ./assets
RUN node build.mjs

FROM nginx:alpine
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
HEALTHCHECK --interval=30s --timeout=3s CMD wget -q -O /dev/null http://127.0.0.1/healthz || exit 1
