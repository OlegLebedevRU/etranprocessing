FROM node:22-alpine@sha256:76789712cd1ae89a1225eac9077010d68987a423588042dac30446f502f1858c AS builder

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY index.html tsconfig.json vite.config.ts ./
COPY src/ ./src/
COPY public/ ./public/
RUN npm test && npm run build

# Artifact image only: production extracts /dist into the existing Nginx mount.
FROM scratch
COPY --from=builder /app/dist /dist