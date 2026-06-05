# MIA Video Protocol PoC

This disposable browser PoC was used to explore an earlier MIA video protocol
model with initial snapshots, committed frames, multiple in-flight responses,
and retained frame repair.

The current protocol documents now describe a simpler client-paced readout
model:

- one active client,
- one outstanding update response,
- two dirty maps: active for new writes and pending for the current response,
- ACK cleanup clears the pending map before the ACK event is exposed,
- deterministic fixed-size dirty page records,
- full refresh by scheduling the next response to mark every pending page dirty,
- no explicit 6502 frame commit,
- no `max_in_flight` parameter.

Open `index.html` in a browser to inspect the old visual bandwidth model, but do
not treat it as the normative implementation of
[`../video-protocol.md`](../video-protocol.md). The PoC should be updated or
replaced before it is used for protocol validation again.
