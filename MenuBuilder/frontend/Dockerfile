FROM node:22-alpine AS builder

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY index.html tsconfig.json vite.config.ts ./
COPY src/ ./src/
COPY public/ ./public/
RUN npm run build

# Artifact image only: production extracts /dist into the existing Nginx mount.
FROM scratch
COPY --from=builder /app/dist /dist