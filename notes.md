# Design and Implementation Notes

### Peer lifetimes, ids, shared/weak pointers

We want:-
- To have Peers owned (perhaps jointly) by the PeerManager.
- To call poll() and have quick lookups from fd to Peer.
- To have a nice iterator for enumerating Peers without extending their lifetime.
- To have Peer-specific outbound message queues, but preferably owned by the node layer, not by the Peer.

Options:-
- Peer can store a unique Id generated in its constructor
- 

Limitations:-
- std::weak_ptr doesn't have a native operator < for use in a std::map.
- std::owner_less can give insconsistent results for two weak_ptrs that came from two shared_ptrs to the same underlying object.
- If Peers store a unique Id, it can't also implement Peer::FromId nicely.
- 