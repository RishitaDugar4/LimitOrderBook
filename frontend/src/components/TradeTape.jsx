// Rolling tape of recent fills. Newest at the top.
export function TradeTape({ trades }) {
  return (
    <div className="panel tape">
      <div className="panel-title">Trades</div>
      <div className="tape-header">
        <span>Time</span>
        <span>Price</span>
        <span>Qty</span>
        <span>Bid/Ask</span>
      </div>
      <div className="tape-rows">
        {trades.length === 0 && <div className="muted pad">No trades yet</div>}
        {trades.map((t) => (
          <div className="tape-row" key={t.key}>
            <span className="muted">{fmtTime(t.ts)}</span>
            <span className="price">{t.price}</span>
            <span className="qty">{t.quantity}</span>
            <span className="ids">
              #{t.bid.order_id}/#{t.ask.order_id}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}

function fmtTime(ts) {
  if (!ts) return '';
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString('en-US', { hour12: false }) +
    '.' + String(d.getMilliseconds()).padStart(3, '0');
}