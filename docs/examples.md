# Code Examples

Here we show several code snippets from the Hornet codebase to give a feel for the style, design, and effectiveness of the code. Each example illustrates some of Hornet's [design principles](index.md#design).

---

## Bitcoin Script assembly and execution

This unit test builds a very simple script and executes it using Hornet's virtual machine.

```C++
TEST(ScriptTest, RunSimpleScript) {
    // Build a Bitcoin script to evaluate the expression (21 + 21) == 42.
    const auto script = Writer{}.PushInt(21).
                                 PushInt(21).
                                 Then(Op::Add).
                                 PushInt(42).
                                 Then(Op::Equal).Release();

    // Execute the script using the stack-based virtual machine.
    const auto result = Processor{script}.Run();

    // Assert that the script execution completed without error.
    ASSERT_TRUE(result);
    
    // Check the result of execution is 'true'.
    EXPECT_EQ(*result, true);
}
```
> ***Design principles: Conciseness and clarity.*** 
---

## Script opcode handlers

Inside the script runtime, the handler for the `OP_ADD` instruction is written like this:

```C++
// Op::Add
static void OnAdd(const Context& context) {
  BinaryInt32(context, [](int64_t a, int64_t b) { return a + b; });
}
```
> ***Design principles: Separation of concerns.***

Instructions are dispatched to handlers using statically built function pointer tables. No big switch statement on the opcode, just a fast lookup.

---

## Counting sigops with script views

In this function we count the number of pubkeys in a script. We factor out the parsing of the script as a separate concern, and have our `script::View` implement an `Instructions()` method that returns a forward-iterable collection over the instructions in the script. We can then trivially visit each instruction to categorize it by its opcode.

```C++
inline int GetSigOpCount(std::span<const uint8_t> script) {
  using protocol::script::lang::Op;

  int count = 0;
  const protocol::script::View view{script};
  for (const auto& instruction : view.Instructions()) {
    const auto op = instruction.opcode;
    if (op == Op::CheckSig || op == Op::CheckSigVerify)
      ++count;
    else if (op == Op::CheckMultiSig || op == Op::CheckMultiSigVerify)
      count += constants::kMaxPubKeysPerMultiSig;  // = 20
  }
  return count;
}
```
> ***Design principles: Separation of concerns.***
---

## Protocol message loop

This is how the main message pump looks for the wire protocol. Messages are popped from per-peer queues and dispatched to polymorphic message handlers. No switch statements on message type.

```C++
// Process queued inbound messages until the timeout expires.
// Using a sensible timeout prevents starvation of other duties for this thread.
void ProtocolLoop::ProcessMessages(const util::Timeout& timeout) {
  // Create a snapshot of peers and shuffle order for fairness:
  // A noisy peer may dominate this frame, but shuffling prevents systemic bias.
  const auto peers = peers_.Snapshot(/*shuffle=*/true);

  // Iterate over per-peer message inbox queues.
  for (const auto peer : peers) {
    if (peer->IsDropped() || !timeout) continue;
    auto& inbox = inboxes_[peer->GetId()];

    // Per-peer fault isolation so that one bad peer doesn't affect others.
    try {
      while (timeout && !inbox.empty()) {
        auto message = std::move(inbox.front()); inbox.pop();
        for (EventHandler* handler : event_handlers_)
          message->Notify(*handler);  // Double-dispatch via visitor pattern.
      }
    } catch (const std::exception& e) {
      // Treat all unhandled exceptions as protocol violations and drop the peer.
      peer->Drop();
    }
  }
}
```
> ***Design principles: Robustness, modularity, polymorphism.***

An example message handler looks like this:

```C++
void PeerNegotiator::OnMessage(const protocol::message::Ping& ping) {
  Reply<protocol::message::Pong>(ping, ping.GetNonce());
}
```
---

## Handshake state machine

A finite state machine (FSM) is used to encapsulate and simplify the logic for peer handshakes without littering the message handlers with complex `if` statements and introducing potential for bugs. The handlers defer the logic to the `Handshake` object and act on whatever action it instructs.

```C++
// Advances the Handshake state machine and performs any necessary resulting actions.
void PeerNegotiator::AdvanceHandshake(net::SharedPeer peer,
                                 protocol::Handshake::Transition transition) {
  auto& handshake = peer->GetHandshake();

  // Run the state machine forward until we complete or must wait for new input.
  auto action = handshake.AdvanceState(transition);
  while (action.next != protocol::Handshake::Transition::None) {
    Send(peer, protocol::MessageFactory::Default().Create(action.command));
    action = handshake.AdvanceState(action.next);
  }

  // Once the handshake is complete, send our preference notifications.
  if (handshake.IsComplete()) SendPeerPreferences(peer);
}

void PeerNegotiator::OnMessage(const protocol::message::Verack& verack) {
  AdvanceHandshake(GetPeer(verack), protocol::Handshake::Transition::ReceiveVerack);
}
```
> ***Design principles: Encapsulation.***
---

## Header consensus validation with ancestry view

This example shows the worker thread loop for header download and validation. 

We want consensus logic to be canonical and isolated from implementation details, but we need the consensus layer to be able to retrieve timechain-specific data like the median of the past eleven blocks' timestamps. To resolve this layering conflict, we create an abstract interface at the consensus level to represent the concepts used by consensus logic.

```C++
namespace hornet::consensus {

namespace constants {
inline constexpr int kBlocksForMedianTime = 11;
}  // namespace constants

// Represents a read-only view onto the ancestors of a candidate block header.
// Height 0 corresponds to genesis. The highest accessible height is the parent
// of the header currently being validated.
class HeaderAncestryView {
 public:
  virtual ~HeaderAncestryView() = default;

  // Returns the length of the current chain.
  virtual int Length() const = 0;

  // Returns the timestamp of an ancestor at the given height.
  virtual uint32_t TimestampAt(int height) const = 0;

  // Returns the last `count` ancestor timestamps ending at the current tip,
  // ordered from oldest to newest. Does not include the candidate for validation.
  // May return fewer than `count` items if not all exist.
  virtual std::vector<uint32_t> LastNTimestamps(int count) const = 0;

  uint32_t MedianTimePast() const {
    const auto timestamps = LastNTimestamps(constants::kBlocksForMedianTime);
    Assert(!timestamps.empty());  // Impossible: would imply trying to validate the genesis.
    return timestamps[timestamps.size() / 2];
  }
};

}  // namespace hornet::consensus
```
> ***Design principles: One-way dependencies between modules. A lower layer (`consensus`) may not depend on a higher layer (`data`).***

Then in the `data` layer, we implement this interface in a lightweight adapter that references our `HeaderTimechain` data structure. Now we can have our header timechain create one of these adapters when we call `GetValidationView` as seen below. This is the worker thread loop for header download and validation.

```C++
// Validates queued headers, and adds them to the headers timechain.
inline void HeaderSync::Process() {

  for (std::optional<Item> item; (item = queue_.WaitPop());) {
    if (!item->batch.empty()) {
      // As soon as we pop from the queue, request new headers if appropriate.
      RequestHeadersFrom(item->weak_peer);


      // Locates the parent of this header in the timechain.
      auto headers = timechain_.ReadHeaders();
      auto parent = headers->Search(item->batch[0].GetPreviousBlockHash());
      if (!parent) {
        HandleError(*item, item->batch[0], consensus::HeaderError::ParentNotFound);
        continue;
      }


      // Creates an implementation-independent view onto the timechain history for the validator.
      const std::unique_ptr<data::HeaderTimechain::ValidationView> view =
          headers->GetValidationView(parent);

      for (const auto& header : item->batch) {
        // Validates the header against consensus rules.
        const auto validated = consensus::ValidateDownloadedHeader(*parent, header, *view);

        // Handles consensus failures, breaking out of this batch.
        if (const auto* error = std::get_if<consensus::HeaderError>(&validated)) {
          // Notifies caller of consensus failure and discards future batches from the same peer.
          HandleError(*item, header, *error);
          break;
        }

        // Adds the validated header to the headers timechain.
        const auto& context = std::get<model::HeaderContext>(validated);
        view->SetTip(parent = timechain_.AddHeader(parent, context));
      }
    }


    // Update live metrics
    util::NotifyMetric("sync/headers", {{"headers_validated", timechain_.ReadHeaders()->ChainLength()}});

    // Notify if the sync is complete.
    if (!IsFullBatch(item->batch)) {
      handler_.OnComplete(item->weak_peer);
    }
  }
}
```

Finally, the `HeaderAncestryView` is then used inside the consensus layer for header validation, with no dependency at all on the data layer.

```C++
[[nodiscard]] inline HeaderResult ValidateDownloadedHeader(const model::HeaderContext& parent,
                                                           const protocol::BlockHeader& header,
                                                           const HeaderAncestryView& view) {
  const int height = parent.height + 1;

  // Verify previous hash
  if (parent.hash != header.GetPreviousBlockHash()) return HeaderError::ParentNotFound;

  // Verify PoW target is valid and is achieved by the header's hash.
  const auto hash = header.ComputeHash();
  const auto target = header.GetCompactTarget().Expand();
  if (!(hash <= target)) return HeaderError::InvalidProofOfWork;

  // Verify PoW target obeys the difficulty adjustment rules.
  if (header.GetCompactTarget() != AdjustCompactTarget(height, parent.data, view))
    return HeaderError::BadDifficultyTransition;

  // Verify median of recent timestamps.
  if (header.GetTimestamp() <= view.MedianTimePast()) return HeaderError::BadTimestamp;

  // Verify that the timestamp isn't too far in the future.
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  if (std::chrono::seconds{header.GetTimestamp()} >
      now + std::chrono::seconds{constants::kTimestampTolerance})
    return HeaderError::BadTimestamp;

  // Verify that the version number is allowed at this height.
  if (!detail::IsVersionValidAtHeight(header.GetVersion(), height)) return HeaderError::BadVersion;

  return parent.Extend(header, hash);
}
```
---

## Node orchestration

Hornet Node's `main.cpp` is very simple as it is a lightweight wrapper around the library components. The node behavior is encapsulated in a class `hornet::node::Controller` and is called like this. The lambda function passed into `Run()` determines when the message loop should break and return.

```C++
std::atomic<bool> is_abort = false;

Controller controller;
controller.SetConnectAddress(options.connect);
// Other runtime options
controller.Initialize();
controller.Run([&]() { 
    return is_abort.load(); 
});
```
> ***Design principles: modern and reuseable.***

----
> **These examples illustrate Hornet Node's design philosophy: conciseness, clarity, rigor, modularity, and efficiency.**