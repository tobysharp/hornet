# UTX-O(1): Constant-Time Unspent Transaction Queries for Bitcoin Clients

## Toby Sharp, Hornet Node

## 1. Abstract

## 2. Method

### Data Structures

**Outputs Table**. The outputs table is a large disk-resident table of all transaction outputs in chronological order. Each entry contains the output's block height, coinbase flag, value amount, and its variable-length `pkScript`. The table is distributed across multiple files (or *segments*) on disk, and is an append-only data structure. The only time the table will be compacted is if the user explicitly launches a `Compact` operation (see below). 

**Outputs Index**. The outputs index is an index that maps from an `OutPoint` to a 64-bit logical byte offset into the outputs table. The index is split across multiple *shards* to allow for parallelism during queries. Each shard contains a *directory* that partitions the shard into slices. See below for details.

### A. Append



### B. Query

### C. Merge

### D. Rewind

### E. Compact

## References