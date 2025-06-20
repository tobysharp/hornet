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

### Outbound message queues

We need a good design to enable shared messages between peers, and seamless back-pressure for on-demand processing, and good parallelization.

We could explore an outbound work item representation:

```
class OutboundMessage {
    std::unique_ptr<protocol::Message> msg;
    std::shared_ptr<std::vector<uint8_t>> framed;
    std::unordered_set<std::weak_ptr<net::Peer>, std::owner_less<>> targets;
    
    bool IsIncluded(std::shared_ptr<net::Peer>) const;
};
using OutboundQueue = std::map<std::weak_ptr<net::Peer>, 
 std::shared_ptr<OutboundMessage>, std::owner_less<>>;
```

Alternative:


```
class PerPeerSink {

};
```

### Request Tracking

When we send outbound request messages like `getheaders`, `ping`, etc.,
when the outbound message is serialized, instead of deleting it, we
add it to a request tracking map, by peer. Then, when a request response
message like `headers`, `pong`, etc. arrives, we search in the tracking map
for a matching request issued to the same peer. If found, we remove the
request from the map and continue to process the response. Otherwise, we
ignore the inbound message, or penalize/disconnect the peer. 
In `protocol::Message`, we add
```
     virtual bool IsTrackedRequest() const { return false; }
     virtual bool IsMatchingRequest(const Message* request) const { return false; }
```
And we add a new `RequestTracker` class with, e.g.
```
     void Track(const OutboundMessage&);
     bool Match(const InboundMessage&);
```
However, we will defer implementing this until after header sync and validation.