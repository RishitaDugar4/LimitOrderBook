import { useOrderBook } from './useOrderBook';
import { OrderBookLadder } from './components/OrderBookLadder';
import { TradeTape } from './components/TradeTape';
import { OrderEntry } from './components/OrderEntry';

export default function App() {
  const { book, trades, connected } = useOrderBook();

  return (
    <div className="app">
      <header className="topbar">
        <h1>Limit Order Book</h1>
        <span className={`conn ${connected ? 'up' : 'down'}`}>
          {connected ? 'live' : 'reconnecting…'}
        </span>
      </header>
      <main className="grid">
        <OrderBookLadder book={book} />
        <TradeTape trades={trades} />
        <OrderEntry />
      </main>
      <footer className="foot">
        C++ matching engine · pybind11 · FastAPI · WebSocket
      </footer>
    </div>
  );
}