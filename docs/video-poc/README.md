# MIA Video Protocol PoC

This browser PoC simulates the current dirty-page video protocol described in
[`../video-protocol.md`](../video-protocol.md).

It models:

- one active client session,
- one outstanding update response,
- a 68,944-byte MIA video RAM region and a complete client mirror,
- two 270-byte dirty maps, active and pending,
- fixed 34-byte dirty page records,
- a 32-byte protocol header inside the accepted UDP payload,
- deterministic `FRAME_DATA` chunk order,
- `NACK_CHUNKS` repair from the retained pending page list,
- `ACK_RESPONSE` cleanup and lost-ACK implicit acknowledgement,
- full refresh by setting every pending page dirty on the next accepted request.

Open `index.html` in a browser. The left canvas is live MIA RAM and the right
canvas is the client mirror after protocol updates have been applied.

The canvas captions split logical frame rates from browser drawing rate:

- `RAM FPS` is the simulated 6502/MIA RAM update rate.
- `Apply FPS` is the rate of complete protocol responses applied by the client.
- `Draw FPS` is the browser render-loop rate for the canvas output.

If `Draw FPS` is low while `RAM FPS` and `Apply FPS` are healthy, the bottleneck is
the PoC rendering/DOM loop rather than the protocol network simulation.
The protocol simulation uses a fixed virtual timestep, so it can continue to
advance even when the browser paints fewer canvas frames.

The latency slider is one-way packet delivery delay. The repair slider is the
client's quiet-period timeout before it asks MIA to resend missing chunks with
`NACK_CHUNKS`.
