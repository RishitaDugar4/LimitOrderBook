import { useEffect, useRef, useState, useCallback } from 'react';
import { wsUrl } from './api';

// Maintains a live view of the book plus a rolling trade tape by consuming
// the backend WebSocket. Auto-reconnects with backoff.
export function useOrderBook({ maxTrades = 100 } = {}) {
  const [book, setBook] = useState({ bids: [], asks: [] });
  const [trades, setTrades] = useState([]);
  const [connected, setConnected] = useState(false);
  const wsRef = useRef(null);
  const retryRef = useRef(0);

  const connect = useCallback(() => {
    const ws = new WebSocket(wsUrl());
    wsRef.current = ws;

    ws.onopen = () => {
      setConnected(true);
      retryRef.current = 0;
    };
    ws.onclose = () => {
      setConnected(false);
      const delay = Math.min(1000 * 2 ** retryRef.current, 10000);
      retryRef.current += 1;
      setTimeout(connect, delay);
    };
    ws.onerror = () => ws.close();
    ws.onmessage = (evt) => {
      const msg = JSON.parse(evt.data);
      if (msg.type === 'book') {
        setBook(msg.book);
      } else if (msg.type === 'trades') {
        setTrades((prev) => {
          const stamped = msg.trades.map((t, i) => ({
            ...t,
            ts: msg.ts,
            key: `${msg.ts}-${i}-${t.bid.order_id}-${t.ask.order_id}`,
          }));
          return [...stamped.reverse(), ...prev].slice(0, maxTrades);
        });
      }
    };
  }, [maxTrades]);

  useEffect(() => {
    connect();
    return () => {
      if (wsRef.current) {
        wsRef.current.onclose = null; // prevent reconnect on unmount
        wsRef.current.close();
      }
    };
  }, [connect]);

  return { book, trades, connected };
}