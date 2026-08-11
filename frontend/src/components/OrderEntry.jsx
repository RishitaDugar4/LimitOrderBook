import { useState } from 'react';
import { api } from '../api';

const ORDER_TYPES = [
  'GoodTillCancel',
  'FillAndKill',
  'FillOrKill',
  'GoodForDay',
  'Market',
];

// Order-entry ticket: submit limit/market orders and cancel by id.
export function OrderEntry() {
  const [side, setSide] = useState('Buy');
  const [orderType, setOrderType] = useState('GoodTillCancel');
  const [price, setPrice] = useState('100');
  const [quantity, setQuantity] = useState('10');
  const [cancelId, setCancelId] = useState('');
  const [status, setStatus] = useState(null);
  const [busy, setBusy] = useState(false);

  const isMarket = orderType === 'Market';

  async function submit(e) {
    e.preventDefault();
    setBusy(true);
    setStatus(null);
    try {
      const qty = parseInt(quantity, 10);
      let res;
      if (isMarket) {
        res = await api.submitMarket({ side, quantity: qty });
      } else {
        res = await api.submitLimit({
          order_type: orderType,
          side,
          price: parseInt(price, 10),
          quantity: qty,
        });
      }
      const filled = res.trades.reduce((s, t) => s + t.quantity, 0);
      setStatus({
        ok: true,
        msg: `Order #${res.order_id} accepted · ${res.trades.length} trade(s), ${filled} filled`,
      });
    } catch (err) {
      setStatus({ ok: false, msg: err.message });
    } finally {
      setBusy(false);
    }
  }

  async function cancel(e) {
    e.preventDefault();
    if (!cancelId) return;
    setBusy(true);
    setStatus(null);
    try {
      await api.cancel(parseInt(cancelId, 10));
      setStatus({ ok: true, msg: `Cancel sent for #${cancelId}` });
      setCancelId('');
    } catch (err) {
      setStatus({ ok: false, msg: err.message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="panel entry">
      <div className="panel-title">Order Entry</div>
      <form onSubmit={submit}>
        <div className="side-toggle">
          <button
            type="button"
            className={`buy ${side === 'Buy' ? 'active' : ''}`}
            onClick={() => setSide('Buy')}
          >
            Buy
          </button>
          <button
            type="button"
            className={`sell ${side === 'Sell' ? 'active' : ''}`}
            onClick={() => setSide('Sell')}
          >
            Sell
          </button>
        </div>

        <label>
          Type
          <select value={orderType} onChange={(e) => setOrderType(e.target.value)}>
            {ORDER_TYPES.map((t) => (
              <option key={t} value={t}>{t}</option>
            ))}
          </select>
        </label>

        <label>
          Price
          <input
            type="number"
            value={price}
            disabled={isMarket}
            onChange={(e) => setPrice(e.target.value)}
          />
        </label>

        <label>
          Quantity
          <input
            type="number"
            min="1"
            value={quantity}
            onChange={(e) => setQuantity(e.target.value)}
          />
        </label>

        <button className={`submit ${side.toLowerCase()}`} disabled={busy}>
          {side} {isMarket ? 'Market' : quantity + ' @ ' + price}
        </button>
      </form>

      <form className="cancel-form" onSubmit={cancel}>
        <label>
          Cancel by Order ID
          <div className="cancel-row">
            <input
              type="number"
              value={cancelId}
              placeholder="order id"
              onChange={(e) => setCancelId(e.target.value)}
            />
            <button className="cancel-btn" disabled={busy || !cancelId}>
              Cancel
            </button>
          </div>
        </label>
      </form>

      {status && (
        <div className={`status ${status.ok ? 'ok' : 'err'}`}>{status.msg}</div>
      )}
    </div>
  );
}