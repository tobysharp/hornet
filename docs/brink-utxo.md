---
marp: true
theme: hornet
paginate: false
---

# The Custom UTXO Database

3 primary operations designed specifically for the consensus workload, callable <span class="orange">out of order</span>,  <span class="orange">concurrently</span>, and  <span class="orange">lock-free</span>:

1. **Append**: adds a block's new outputs and tombstones its consumed outputs.
2. **Query**: takes input `OutPoint`s and returns their record addresses (`OutputId`).
3. **Fetch**: takes `OutputId`s and returns the output data itself.

```cpp
class Database {
 public:
  Pin Append(const protocol::Block& block, int height);

  QueryResult Query(std::span<const OutPoint> keys, std::span<OutputId> rids, int since, int before) const;

  int Fetch(std::span<const OutputId> rids, std::span<OutputDetail> outputs, 
            std::vector<uint8_t>* scripts) const;

  void EraseSince(int height);
};
```

<!-- Presenter: name the four behaviors, then the claim — concurrent, no sequential sync point, earned over the next slides starting with append. The [since, before) window on Query is what lets a query resolve against only the committed prefix; explained on the append slide. rid / id is the in-code name for OutputId. -->

---

# Append

```cpp
  Pin Append(const protocol::Block& block, int height);
```

- Adds a block's new outputs and tombstones its consumed outputs.
- Append-only is cheap and concurrent -- no inserting, sorting, or waiting.
- Block can be appended **out of order** -- removes sequential constraints.
- Blocks are added **speculatively** -- unblocking descendants immediately.
- Since entries are height-tagged, invalid branches and reorgs are trivially erased.

---

# Query

```cpp
  QueryResult Query(std::span<const OutPoint> keys, std::span<OutputId> rids, int since, int before) const;
```

The database **index** is comprised of many sorted key-value entries:

```cpp
struct OutputKV {  
  OutPoint key;         // Query key (txid + outindex): 36 bytes
  struct {              //
    int height : 31;    // Block height of entry
    int op     :  1;    // Tombstone flag
  } data;               //                               4 bytes
  OutputId rid;         // Data storage location:        8 bytes
};                      // Total:                       48 bytes     
```

Query keys for a block are first **sorted**. 

Then an index of sorted KV entries would enable **doubly-sorted binary search**.

But how can we keep our ~200M index elements sorted when the set is updating with each block?

---

# The UTXO Index: Sorted Runs

Each `Append` creates an in-memory **run**: a key-sorted, page-allocated span of `OutputKV` entries.

The run contains a **Bloom filter**: a bitmask that enables fast exclusion without false negatives.

A run also holds a **prefix dictionary** that maps a key prefix to a single-page key range.

Then search is **monotonic** within the sorted run, accessing ~1 page of RAM per key.
```cpp
for (auto key : keys) {
  // Check the Bloom filter for quick exit, with false positive rate ~0.01.
  if (!filter_.MayContain(key)) continue;

  // Tighten bounds via the prefix directory.
  const auto [lo, hi] = directory_.LookupRange(key);
  lower = std::max(lower, entries_.begin() + lo);  // Lower bound is monotonically increasing...
  upper = entries_.begin() + hi;                   // while upper bound resets for each key.

  // Binary search in the remaining range for the first item that's ordered >= the query key.
  auto it = std::lower_bound(lower, upper, key);
}
```

---

# The UTXO Index: Hierarchical Ages

The in-memory runs for each block are **grouped** into an `Age` (age 0).
**Background compaction** merges $k$ runs from one age into a $k\times$ larger sorted run in the next age.
During compaction, **tombstones are cancelled** when they merge with their counterpart$^*$.
A full query visits each age and run from newest to oldest until all keys are resolved.

![h:350](runs_ages_blocks_top.svg)

$^*$<span class="footnote">Except in mutable ages.</span>

---

# Fetch 

```cpp
  int Fetch(std::span<const OutputId> rids, std::span<OutputDetail> outputs, 
            std::vector<uint8_t>* scripts) const;
```
`OutputId` is a packed 64-bit `{offset, length}` logical byte address pair.

The database **table** consists of multiple **disk files** and an **in-memory tail**, storing for each output:
- amount, coinbase flag, and script bytes.

Data is **appended** to the in-memory tail array, and **flushed** to disk in a background thread.

**Reads** are batched into asynchronous `io_uring` requests at high queue depth, for NVMe parallelism. 

~1 page per I/O request without OS page cache pollution.

---

# Mutable and Immutable Data

TODO 

---

# SingleWriter: Lock-Free Readers and Serialized Writers

## Copy-Mutate-Publish on Write

```cpp
template <class T> class SingleWriter {
  // Returns a read-only snapshot of the current state (lock-free).
  std::shared_ptr<const T> Snapshot() const { return std::atomic_load_explicit(&ptr_, ...); }

  // Returns a mutable copy of the current state (lock-free).
  [[nodiscard]] std::shared_ptr<T> Copy() const { return std::make_shared<T>(*Snapshot()); }

  // Operates on a read-only snapshot of the current state (lock-free).
  const T* operator ->() const { return Snapshot().get(); }

  // Atomically publishes a new version, ignoring other writers that may be mutating snapshots (lock-free).
  void Publish(std::shared_ptr<const T> ptr) { std::atomic_store_explicit(&ptr_, std::move(ptr), ...); }

  // `Writer` holds an exclusive lock, makes a copy, and publishes the mutated object when scope ends.
  Writer Edit() { return {*this}; }

  mutable std::mutex mutex_;
  std::shared_ptr<const T> ptr_;
};
```

---

# Per-block spend resolution

## `SpendJoiner`: the resolver state machine

- One instance per block; resolves the block's input prevouts against accumulated chain state.
- `Advance()` executes one state transition: it calls the `Database` and updates the joiner's state.
- `QueriedPart` / `FetchedPart` handle ancestors not yet committed: resolve the committed prefix, retain the partial result, complete the remainder when the ancestors arrive.

```cpp
class SpendJoiner {
 public:
  enum class State { Init, Parsed, Appended, QueriedPart, Queried, FetchedPart, Fetched, Joined, ... };
  StepResult Advance();   // one transition: call into Database, mutate own state
  ...
};
```

<!-- Presenter: emphasise that the partial result is retained — no recomputation when the ancestor lands. -->

---

# `SpendJoiner` State Machine

`SpendJoiner` manages the database operations required for one block.

![](spendjoiner_state_machine.svg)

<!-- Presenter: walk the spine left to right, then the dashed hold below as the late-ancestor case. -->

---

# Resolving blocks in parallel and out of order

## `SpendPipeline`: the worker thread pool

A worker pool with **ready** and **blocked** job queues, driving many joiners at once:

```cpp
void SpendPipeline::WorkerLoop() {
  while (true) {
    ...
    job = ready_queue_.Pop();
    auto [state, action] = job.joiner->Advance();
    if (state == State::Appended) WakeBlockedJobs();      // children can see these outputs now
    if      (action == Action::Advance) ready_queue_.Push(job);
    else if (action == Action::Wait)    blocked_list_.push_back(job);
    else                                job.on_complete(job.joiner);
    ...
  }
}
```

---

# UTXO Engine Summary

- Concurrent processing with lock-free Query and Fetch.
- Out-of-order and speculative append for non-sequential operations.
- Cache-friendly batched, sorted binary search queries.
- Single-page per key memory access on `Query`.
- Single-page per key disk access on `Fetch`.
- Single contiguous disk write on background table flush.
- `Erase` / reorg without separate undo data.
- Mainnet memory usage ~8 GiB.
- Over 11x performance measured vs Core v30 on `-reindex-chain-state -assumevalid`.
- Functions as a polymorphic backend to the consensus interface contract.

---


<!-->
<!--
![](spendpipeline.svg)

<!-- Presenter: routing is driven by the returned Action; the amber arc is speculative append releasing parked children. -->
<!--
---

# Store, Query, Fetch

## `Database`: the index and table

- `OutputId` is assigned by the `Table` on append and stored by the `Index`; the stores share no other state.
- `Append` writes both; `Query` reads the `Index`; `Fetch` reads the `Table`; `EraseSince` reverts a reorg.
- `Append` holds a shared lock; `Query` and `Fetch` are lock-free; `EraseSince` is exclusive.

```cpp
class Database {
 ...
 private:
  Table table_;   // address -> bytes   (what is it)
  Index index_;   // key     -> address (where is it)
  ...
};
```

<!-- Presenter: stated here, justified in the next section — the Table and Index internals. -->
<!--
---

---

<!-- _class: section -->
<!--
# The UTXO Engine

Validation needs the chain's unspent outputs. This is how Hornet stores and resolves them — and why the specification is what makes the design free.

---

# Resolving spent prevouts

## `ChainOutputsView`: the consensus view

- Consensus resolves a block's spent previous outputs only through `ChainOutputsView`.
- The interface fixes the spending conditions; it does not constrain their implementation.
- The four methods are pure and synchronous to the caller; the implementation behind them may be asynchronous, out-of-order, and concurrent.

```cpp
class ChainOutputsView {                // Consensus sees only this pure interface.
 public:
  virtual bool QueryOutPointsUnique(const Block&) const = 0;        // S01 / BIP30.
  virtual bool QueryPreviousOutputsCreated(const Block&) const = 0; // S02.
  virtual bool QueryPreviousOutputsUnspent(const Block&) const = 0; // S03.
  virtual std::optional<JoinedSpendRange> Spends(const Block&) const = 0;
};
```

<!-- Presenter: this interface is the boundary between specification and implementation. The spec fixes the validity conditions; everything below this line is unconstrained — which is what permits the design that follows. -->
<!--
---

# Per-block spend resolution

## `SpendJoiner`: the resolver state machine

- **Append** — record the block's effect on the set: write its new outputs, and write tombstones for the prevouts it spends.
- **Query** — resolve a prevout (txid, output index) to the location of its output data.
- **Fetch** — read the output data from that location.
- **Join** — attach the fetched output data to the spending inputs that reference it.
- One `SpendJoiner` per block sequences these as a state machine; `Advance()` performs one step, with partial queries when ancestor data is not yet committed.

```cpp
class SpendJoiner {
 public:
  enum class State { Init, Parsed, Appended, QueriedPart, Queried, FetchedPart, Fetched, Joined, ... };
  StepResult Advance();   // one transition: call into Database, mutate own state
  ...
};
```

<!-- Presenter: define the four operations clearly — they're not obvious and the rest of the section relies on them. The state machine just sequences them. -->
<!--
---

![](spendjoiner_state_machine.svg)

<!-- Presenter: walk the spine left to right naming each operation, then the dashed hold below as the late-ancestor case — this is where QueriedPart / FetchedPart live. -->
<!--
---

# Resolving blocks in parallel and out of order

## `SpendPipeline`: the worker thread pool

- **Speculative append** — a block's new outputs and spend tombstones are written *before* the block is validated. (An invalid block is rewound with `EraseSince`.)
- **Out-of-order append** — a block may be appended before its ancestors, so processing is not forced to be sequential.
- **Partial resolution** — a query resolves against the committed prefix, and completes the remainder as the missing blocks arrive.

```cpp
void SpendPipeline::WorkerLoop() {
  while (true) {
    ...
    job = ready_queue_.Pop();
    auto [state, action] = job.joiner->Advance();
    if (state == State::Appended) WakeBlockedJobs();      // waiting blocks can now resolve
    if      (action == Action::Advance) ready_queue_.Push(job);
    else if (action == Action::Wait)    blocked_list_.push_back(job);
    else                                job.on_complete(job.joiner);
    ...
  }
}
```

<!-- Presenter: out-of-order append is the direct answer to the total-ordering objection, if it comes up in Q&A. -->
<!--
---

![](spendpipeline.svg)

<!-- Presenter: routing is driven by the returned Action; the amber arc is speculative append releasing parked blocks. -->
<!--
---

# Store, query, fetch

## `Database`: the index and table

- Two stores, linked only by the `OutputId`.
- **Index** — maps an outpoint to its `OutputId`: the query step.
- **Table** — holds the output data at that `OutputId`: the fetch step.

```cpp
class Database {
 ...
 private:
  Table table_;   // OutputId -> bytes      (data)
  Index index_;   // OutPoint -> OutputId   (location)
  ...
};
```

<!-- Presenter: stated here, justified in the next section — the Table and Index internals. -->