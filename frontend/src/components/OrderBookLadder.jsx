// L2 depth ladder: asks descending on top, bids descending below, with a
// cumulative-depth bar behind each level.
export function OrderBookLadder({ book }) {
  const asks = [...(book.asks || [])]; // lowest-first from backend
  const bids = [...(book.bids || [])]; // highest-first from backend

  const maxQty = Math.max(
    1,
    ...asks.map((l) => l.quantity),
    ...bids.map((l) => l.quantity)
  );

  const bestBid = bids[0]?.price;
  const bestAsk = asks[0]?.price;
  const spread =
    bestBid != null && bestAsk != null ? bestAsk - bestBid : null;

  // Show asks with the best (lowest) ask nearest the spread in the middle.
  const asksDisplay = [...asks].reverse();

  return (
    <div className="panel ladder">
      <div className="panel-title">Order Book</div>
      <div className="ladder-header">
        <span>Price</span>
        <span>Qty</span>
      </div>

      <div className="ladder-side asks">
        {asksDisplay.map((l) => (
          <Row key={`a-${l.price}`} level={l} side="ask" maxQty={maxQty} />
        ))}
      </div>

      <div className="ladder-spread">
        {spread != null ? (
          <>
            <span>{bestBid} × {bestAsk}</span>
            <span className="spread-val">spread {spread}</span>
          </>
        ) : (
          <span className="muted">— no two-sided market —</span>
        )}
      </div>

      <div className="ladder-side bids">
        {bids.map((l) => (
          <Row key={`b-${l.price}`} level={l} side="bid" maxQty={maxQty} />
        ))}
      </div>
    </div>
  );
}

function Row({ level, side, maxQty }) {
  const pct = Math.round((level.quantity / maxQty) * 100);
  return (
    <div className={`ladder-row ${side}`}>
      <div className="depth-bar" style={{ width: `${pct}%` }} />
      <span className="price">{level.price}</span>
      <span className="qty">{level.quantity}</span>
    </div>
  );
}