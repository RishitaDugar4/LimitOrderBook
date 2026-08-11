# Build the React app, then serve the static bundle with nginx (which also
# reverse-proxies /api and /ws to the backend container).
FROM node:22-alpine AS build
WORKDIR /app
COPY frontend/package.json frontend/package-lock.json* ./
RUN npm install --no-audit --no-fund
COPY frontend/ ./
# Same-origin in production: leave VITE_API_BASE empty so the app calls /api
# and /ws, which nginx proxies to the backend.
RUN npm run build

FROM nginx:1.27-alpine AS runtime
COPY frontend/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]