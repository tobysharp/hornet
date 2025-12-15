#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <ranges>
#include <thread>
#include <vector>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/data/block_io.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetnodelib/util/command_line_parser.h"

using namespace hornet;

template <std::unsigned_integral Index, typename Func>
void ParallelFor(Index start, Index end, Func func) {
  static_assert(std::is_unsigned<Index>::value, "ParallelFor requires unsigned Index type");

  const auto len = end - start;
  if (len == 1) func(start, end);
  if (len <= 1) return;

  // 1. Thread Count = Hardware Cores
  unsigned int thread_count = std::thread::hardware_concurrency();
  if (thread_count == 0) thread_count = 4;

  auto target_chunks = thread_count * 8;
  auto chunk_size = len / target_chunks;
  if (chunk_size * target_chunks < len) ++chunk_size;
  auto total_chunks = len / chunk_size;
  if (total_chunks * chunk_size < len) ++total_chunks;

  // 3. Shared Atomic Counter (The "Queue")
  std::atomic<Index> next_idx{0};

  // 4. The Worker Function
  auto worker = [=, &next_idx, &func]() {
    bool is_continue = true;
    while (is_continue) {
      // A. Claim a chunk index atomically
      const Index chunk_index = next_idx.fetch_add(1, std::memory_order_relaxed);
      if (chunk_index >= total_chunks) break;
    
      // B. Calculate actual range
      const Index offset_start = chunk_index * chunk_size;
      Index offset_end = offset_start + chunk_size;
      if (offset_end < offset_start || offset_end > len) offset_end = len;
      
      // C. Do the work
      is_continue = func(start + offset_start, start + offset_end);
    }
  };

  // Launch threads
  std::vector<std::future<void>> futures;
  futures.reserve(thread_count);
  for (unsigned i = 0; i < thread_count; ++i)
    futures.push_back(std::async(std::launch::async, worker));

  // Current thread doesn't participate because it may be used for display etc. through a callback here.

  // Synchronization
  for (auto& f : futures) f.get();
}

void SetExtraNonce(protocol::Block& block, uint32_t nonce) {
  auto coinbase = block.Transaction(0);
  auto sigscript = coinbase.SignatureScript(0);
  protocol::script::View view{sigscript};
  auto instructions = view.Instructions();
  auto instr = instructions.begin();
  instr++;  // BIP34 push height
  if (instr == instructions.end())
    throw std::runtime_error(
        "Don't know how to interpret a coinbase sigscript with less than two instructions.");
  auto push_nonce = *instr;
  if (push_nonce.data.size() < sizeof(nonce))
    throw std::runtime_error("Coinbase sigscript nonce is less than 4 bytes.");
  const auto nonce_ptr = sigscript.data() + push_nonce.offset + sizeof(protocol::script::lang::Op);
  std::memcpy(nonce_ptr, &nonce, sizeof(nonce));
}

void ComputeHeader(protocol::Block& block, const protocol::Hash& previous) {
  auto header = block.Header();
  header.SetPreviousBlockHash(previous);
  if (header.IsProofOfWork()) return;

  constexpr uint32_t min = std::numeric_limits<uint32_t>::min();
  constexpr uint32_t max = std::numeric_limits<uint32_t>::max();
  std::atomic<bool> done = false;

  for (uint32_t outer = header.GetTimestamp(); !done && outer < max; ++outer) {
    std::cout << "|" << std::flush;
    header.SetTimestamp(outer);
    // header.SetMerkleRoot(consensus::ComputeMerkleRoot(block).hash);

    ParallelFor(min, max, [&](uint32_t begin, uint32_t end) -> bool {
      if (done) return false;
      std::cout << "." << std::flush;
      for (uint32_t nonce = begin; nonce < end; ++nonce) {
        auto h = header;
        h.SetNonce(nonce);
        if (h.IsProofOfWork()) {
          bool old = false;
          if (done.compare_exchange_strong(old, true)) {
            std::cout << " " << h.ComputeHash() << std::flush;
            block.SetHeader(h);
            return false;
          }
        }
      }
      return true;
    });
  }
}

void Run(const std::filesystem::path& in, const std::filesystem::path& out) {
  data::BlockReader reader{in};
  data::BlockWriter writer{out};
  protocol::Hash previous = {};
  for (auto block : reader.Blocks()) {
    std::cout << "Processing: " << std::flush;
    ComputeHeader(*block, previous);
    writer << *block;
    previous = block->Header().ComputeHash();
    std::cout << std::endl;
  }
}

int main(int argc, char** argv) {
  std::string in_path, out_path;

  node::util::CommandLineParser parser{"fixpow", "0.1"};
  parser.AddOption("in", &in_path, "Path to input block file");
  parser.AddOption("out", &out_path, "Path to output block file");
  if (!parser.Parse(argc, argv)) return 1;

  try {
    Run(in_path, out_path);
    std::cout << "Done." << std::endl;
  } catch (const std::exception& e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  return 0;
}
