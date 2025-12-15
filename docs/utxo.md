# Hornet UTXO Database: A High-Performance, Lock-Free, LSM-Style Design

## Executive Summary

This report details the design and implementation of the Hornet UTXO database, a custom-built solution tailored for maximally concurrent, high-throughput Bitcoin consensus validation. Unlike Bitcoin Core, which relies on a general-purpose key-value store (LevelDB) protected by a global lock (`cs_main`), Hornet’s UTXO database is a specialized, lock-free, age-tiered Log-Structured Merge (LSM) tree designed from the ground up to saturate modern NVMe storage and multi-core CPUs.

The design achieves **zero IBD bandwidth limitation due to UTXO lookups**, ensuring that validation speed is limited only by the network or ECDSA verification, not by database latency.

## 1. Architectural Overview

The Hornet UTXO database is split into two primary components:
1.  **The Index (`Index`)**: A memory-optimized, age-tiered structure that maps `OutPoint` (TxID + Index) to `OutputId` (file offset + length).
2.  **The Table (`Table`)**: A flat file storage system (`Segments`) containing the actual `OutputHeader` and script data, optimized for bulk sequential writes and random async reads.

This separation allows the index to remain compact and highly cache-efficient while the bulk data resides on disk, fetched only when needed via high-queue-depth asynchronous I/O.

### 1.1. Age-Tiered LSM Index
The index is organized into **7 "Ages"** (`MemoryAge`).
*   **Mutable Ages (0-2)**: Recent blocks are stored in mutable memory runs. This allows for instant, zero-cost reorgs by simply discarding data above a certain height.
*   **Immutable Ages (3-6)**: Older data is merged into immutable runs.
*   **Background Compaction**: A background `Compacter` thread continuously merges younger ages into older ones, similar to LevelDB's compaction but specialized for UTXO workloads (e.g., removing spent outputs during merge).

## 2. The `SpendJoiner`: Asynchronous Input Gathering

A critical innovation in Hornet is the `SpendJoiner` class, which orchestrates the lifecycle of a block's inputs as they move through the validation pipeline. Instead of a synchronous "stop-the-world" lookup, input gathering is a multi-stage state machine:

1.  **Parse**: The block is scanned to extract all input `OutPoint`s (keys) and output definitions.
2.  **Append**: The block's *new* outputs are optimistically appended to the database (marked with the block's height). This ensures that intra-block spends (transactions spending outputs created earlier in the same block) are handleable.
3.  **Query**: The database index is queried for all input keys in parallel. This step resolves file offsets (`OutputId`) for every input.
4.  **Fetch**: Using the resolved offsets, the raw UTXO data (headers and scripts) is fetched from disk. This uses `io_uring` to batch all reads for the block into a single submission, maximizing I/O throughput.
5.  **Join**: Finally, the inputs are "joined" with the transaction data, creating fully populated `SpendRecord` objects ready for validation.

The `ValidationPipeline` manages these joiners. If a block's data isn't ready (e.g., waiting on disk I/O), the worker thread simply defers the job and picks up another block, ensuring CPU cores never idle waiting for disk.

## 3. Modular Consensus Interface

Hornet strictly separates **data retrieval mechanics** from **consensus rules**. This is achieved via the `UnspentOutputsView` interface.

### 3.1. The Abstract View
The core consensus logic (in `hornet::consensus`) interacts with the UTXO set solely through the `UnspentOutputsView` abstract base class. It defines high-level operations like:
*   `QueryPrevoutsUnspent`: "Do these inputs exist and are they unspent?"
*   `ForEachSpend`: "Iterate over every input in this block and give me the funding details."

### 3.2. The Implementation
The `DatabaseView` implements this interface by wrapping a `SpendJoiner`. When the consensus engine asks to iterate spends, the `DatabaseView` ensures the `SpendJoiner` has reached the `Fetched` state and then yields the data.

**Why this is a win:**
*   **Purity**: The consensus rules (`ValidateBlock`, `ValidateSpending`) are pure functions. They don't know about files, caches, locks, or `io_uring`. They simply validate logic.
*   **Testability**: You can trivially mock `UnspentOutputsView` to test consensus rules with hardcoded scenarios, without spinning up a database.
*   **Flexibility**: The underlying database implementation can be completely swapped (e.g., for a RAM-only version or a distributed store) without changing a single line of consensus code.

## 4. Key Innovations & "Wins" over Bitcoin Core

### 4.1. Lock-Free Concurrency vs. `cs_main`
*   **Bitcoin Core**: Uses a global mutex (`cs_main`) that serializes all access to the UTXO set (`CCoinsView`). This is a major bottleneck for scalability.
*   **Hornet**: Uses a **Copy-On-Write (COW)** mechanism via `SingleWriter`. Readers (validators) obtain a lock-free snapshot of the database. Writers (appenders) operate on a copy and atomically publish changes. This allows multiple validation threads to query the UTXO set in parallel without contention.

### 4.2. Optimized Query Algorithms
*   **Directory Shortcuts**: Each `MemoryRun` includes a `Directory` (hash prefix bucket array) that narrows down the search range to a small section of the vector.
*   **Galloping Search**: Within a bucket, `GallopingBinarySearch` (exponential search) is used instead of standard binary search. This is significantly faster when the target is expected to be near the beginning of the range, which is common in sorted batch queries.
*   **Doubly-Sorted Linear Scans**: Inputs for a block are sorted by key. The database queries are also performed on sorted runs. This turns random lookups into a series of forward scans, maximizing cache locality.

### 4.3. High-Performance I/O (io_uring)
*   **Bitcoin Core**: Relies on LevelDB's synchronous reads or OS page cache.
*   **Hornet**: Implements `UringIOEngine` using Linux's **`io_uring`**.
    *   **Batching**: `Table::Fetch` batches all input fetches for a block into a single submission.
    *   **High Queue Depth**: The system maintains a high queue depth of in-flight read requests, fully saturating NVMe SSD bandwidth.
    *   **Zero-Copy Unpacking**: Data is read directly into a staging buffer and unpacked into the target `OutputDetail` structures.

### 4.4. Native Reorg Support
*   **Bitcoin Core**: Requires "undo data" (revocation files) to roll back the UTXO set during a reorg. This involves reading disk and applying inverse operations.
*   **Hornet**: The "Mutable Window" (Ages 0-2) keeps recent history in memory. A reorg is simply an `EraseSince(height)` operation, which instantly truncates the vectors in the mutable ages. No disk reads or complex undo logic are required for typical reorg depths.

### 4.5. Memory Management & Tiled Vectors
*   **Tiled Layout**: `TiledVector` uses a vector of fixed-size arrays (tiles). This avoids the expensive reallocation and copying of `std::vector` growth. It also ensures pointer stability, allowing other threads to read from older tiles while new ones are being allocated.

## 5. Feature Comparison

| Feature | Bitcoin Core / LevelDB | Hornet Custom DB | Benefit |
| :--- | :--- | :--- | :--- |
| **Concurrency** | Global Lock (`cs_main`) | Lock-Free Snapshots | Linear scaling with CPU cores |
| **Lookups** | $O(\log N)$ (LSM Tree) | $O(1)$ (Directory + Galloping) | Constant time access |
| **I/O** | Synchronous / mmap | Async `io_uring` | Maximize NVMe IOPS |
| **Reorgs** | Disk-based Undo | In-Memory Truncation | Instant reorgs |
| **Memory** | Fragmentation prone | Tiled / Arena-like | Stable performance |
| **Consensus Logic** | Coupled with DB implementation | Pure, declarative rules | Easier testing & verification |
| **Input Gathering** | Stop-the-world lookup | Async `SpendJoiner` pipeline | Zero CPU idle time |

## 6. Conclusion

Hornet's UTXO database represents a paradigm shift from general-purpose storage to a specialized, high-performance engine. By integrating the database deeply with the validation pipeline via the `SpendJoiner`, Hornet transforms the UTXO lookup from a blocking, latency-sensitive operation into a non-blocking, high-bandwidth stream.

The combination of **lock-free concurrency**, **async I/O**, **algorithmic optimizations**, and **modular design** ensures that the database is never the bottleneck, enabling the node to validate the blockchain as fast as the network can deliver it.