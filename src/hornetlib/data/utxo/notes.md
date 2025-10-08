# Notes

## Validation Pipeline

### Concurrent Pipeline Stages

| Height | Stage |
|--|--|
| N | Block structural / contextual validation |
| N-1 | UTXO tail query prevouts, UTXO tail append outputs |
| N-2 | UTXO index query prevouts, Unspent validation |
| N-3 | UTXO table fetch |
| N-4 | UTXO join, script validation |

### Parallelism Within Procedures

| Function | Parallelizes Over |
|--|--|
| Block structural / contextual validation | Validation rules |
| Transaction validation | Transactions
| UTXO tail query | UTXO index shards |
| UTXO tail append outputs | UTXO index shards |
| UTXO index query prevouts | UTXO index shards |
| UTXO table fetch | Asynchronous I/O requests |
| Script validation | Spend joins |

### Worker Thread Pool

- Thread count 1-2x logical core count.
- Procedures call ParallelFor on the thread pool object.
- ParallelFor puts a job/task into the set of current work items.
- When worker threads wake/loop they pick a task at random, and
- Carve out a packet of the remaining work for themselves.
- If they take the last packet of work for that item, they remove the item.
- ParallelFor blocks until the last iteration of a task completes.

### APIs

```cpp

class Database;

class RecentStore {
 public:
  std::vector<uint64_t> Query(std::span<const protocol::OutPoint>) const;
  void Append(const protocol::Block& block, int height);
  void RemoveSince(int height);
 private:
  friend class Database;
  void RemoveBefore(int height);

  Sharded<RecentIndex> index_;
};

class CommittedStore {
 public:
  std::vector<uint64_t> Query(std::span<const protocol::OutPoint>) const;
  void Commit(std::span<const OutputHeader> headers, std::span<const uint8_t> scripts);

 private:
  Sharded<CommittedIndex> index_;
};

class Database {
 public:
  RecentStore& Recent() { return recent_; }
  const RecentStore& Recent() const { return recent_; }
  CommittedStore& Committed() { return committed_; }
  const CommittedStore& Committed() const { return committed_; }

  bool IsCommittable() const;
  void CommitRecent();

  std::pair<std::vector<OutputHeader>, std::vector<uint8_t>> Fetch(
    std::span<const uint64_t> addresses) const;

 private:
  CommittedStore committed_;
  RecentStore recent_;
};
```


