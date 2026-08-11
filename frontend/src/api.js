// REST + WebSocket client for the order-book backend.
//
// URLs: in dev, Vite proxies /api and /ws to the backend (see vite.config.js).
// In production, set VITE_API_BASE (e.g. https://host) at build time; we
// derive the WS URL from it.

const API_BASE = import.meta.env.VITE_API_BASE || '';

function apiUrl(path) {
  // With a base set we hit it directly; otherwise use the /api dev proxy.
  return API_BASE ? `${API_BASE}${path}` : `/api${path}`;
}

export function wsUrl() {
  if (API_BASE) {
    const u = new URL(API_BASE);
    u.protocol = u.protocol === 'https:' ? 'wss:' : 'ws:';
    u.pathname = '/ws';
    return u.toString();
  }
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${window.location.host}/ws`;
}

async function request(path, options) {
  const res = await fetch(apiUrl(path), {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`${res.status}: ${text}`);
  }
  return res.json();
}

export const api = {
  submitLimit: (body) =>
    request('/orders', { method: 'POST', body: JSON.stringify(body) }),
  submitMarket: (body) =>
    request('/orders/market', { method: 'POST', body: JSON.stringify(body) }),
  modify: (body) =>
    request('/orders', { method: 'PUT', body: JSON.stringify(body) }),
  cancel: (orderId) =>
    request(`/orders/${orderId}`, { method: 'DELETE' }),
  book: () => request('/book'),
  recentTrades: (limit = 50) => request(`/history/trades?limit=${limit}`),
};