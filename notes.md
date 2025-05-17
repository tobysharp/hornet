# Design and Implementation Notes

### Peer lifetimes, ids, shared/weak pointers

We want:
- To have `Peer`s owned (perhaps jointly) by the `PeerManager`.
- To call `poll()` and have quick lookups from `fd` to `Peer`.
- To have a nice iterator for enumerating `Peer`s without extending their lifetime.
- To have `Peer`-specific outbound message queues, but preferably owned by the node layer, not by the `Peer`.


Limitations:
- `std::weak_ptr` doesn't have a native `operator <` for use in a `std::map`.
- `std::map` of `std::weak_ptr` requires `std::owner_less<std::weak_ptr<T>>` as the comparator.
- If `Peers` store a unique `Id`, it can't also implement `Peer::FromId` nicely.


Later we may choose to guarantee that `Peer` pointers can't dangle, and just use raw references and pointers. But this requires discipline and careful flushing of queues etc.

### `node::ProtocolThread`

Maybe rename as `node::Engine`.

Refactor into multiple sub-objects and threads.

### Dropping peers

