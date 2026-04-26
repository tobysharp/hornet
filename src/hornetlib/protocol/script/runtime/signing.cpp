#include <span>
#include <vector>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/signing.h"
#include "hornetlib/protocol/script/spend.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime {

namespace {

using lang::Op;

enum class CommitInputs : uint8_t { All = 0, Current = 0x80 };
enum class CommitOutputs : uint8_t { All = 1, None = 2, Single = 3 };

// The commit mode specifies which inputs and outputs we must commit to when building the spend digest for a signature.
struct CommitMode {
  const uint8_t flags = std::to_underlying(CommitInputs::All) | std::to_underlying(CommitOutputs::All);

  constexpr CommitInputs InputsMode() const { return CommitInputs(flags & kInputsMask); }
  constexpr CommitOutputs OutputsMode() const {
    const uint8_t output_flags = flags & kOutputsMask;
    if (output_flags == 2) return CommitOutputs::None;
    if (output_flags == 3) return CommitOutputs::Single;
    return CommitOutputs::All;
  } 
  constexpr bool IsCommitOtherSequences() const { return OutputsMode() == CommitOutputs::All; }

  static constexpr uint8_t kInputsMask = 0x80;
  static constexpr uint8_t kOutputsMask = 0x1f;
};

// This class writes the byte stream that commits to a spending transaction for the spend digest.
class LegacyCommitSerializer {
 public:
  LegacyCommitSerializer(lang::Bytes sigarg) : match_(Writer{}.PushData(sigarg).Release()) {}

  std::vector<uint8_t> operator()(const SpendContext& spend, CommitMode commit, lang::Bytes script_code) {
    writer_.Clear();
    // TODO: writer_.Reserve(...);
    writer_.WriteLE4(spend.tx.Version());
    SerializeInputs(spend, commit, script_code);
    SerializeOutputs(spend, commit);
    writer_.WriteLE4(spend.tx.LockTime());
    writer_.WriteLE4(commit.flags);
    return writer_.ReleaseBuffer();
  }

 private:
  // NOTE: This function's behavior isn't identical to Core when scripts are malformed, but such scripts fail validation
  // anyway.
  void SerializeScriptCode(lang::Bytes script) {
    // Writes a stripped version of the script into a temporary note.
    Writer note;
    for (const auto& instruction : View{script}.Instructions()) {
      if (instruction.opcode != Op::CodeSeparator && !std::ranges::equal(instruction.raw, match_))
        note.Write(instruction.raw);
    }
    const auto stripped = note.Release();

    // Copy the stripped code directly into the main writer.
    writer_.WriteVarInt(std::ssize(stripped));
    writer_.WriteBytes(stripped);
  }

  void SerializeInput(const OutPoint& prevout, lang::Bytes script, uint32_t sequence) {
    prevout.Serialize(writer_);
    SerializeScriptCode(script);
    writer_.WriteLE4(sequence);
  }

  void SerializeInputs(const SpendContext& spend, CommitMode commit, lang::Bytes script_code) {
    switch (commit.InputsMode()) {
      case CommitInputs::Current: {
        // Serializes only the current input.
        writer_.WriteVarInt(1);
        const Input& input = spend.tx.Input(spend.input_index);
        return SerializeInput(input.previous_output, script_code, input.sequence);
      }

      case CommitInputs::All:
        // Serializes all inputs.
        writer_.WriteVarInt(spend.tx.InputCount());
        for (int i = 0; i < spend.tx.InputCount(); ++i) {
          const auto& input = spend.tx.Input(i);
          if (i == spend.input_index)  // Serializes the actual spending input.
            SerializeInput(input.previous_output, script_code, input.sequence);
          else  // Serializes the non-spending input, but removes the sig script / sequence data.
            SerializeInput(input.previous_output, {}, commit.IsCommitOtherSequences() ? input.sequence : 0);
        }
        return;
    }
    Assert(false);
  }

  void SerializeOutput(int64_t value, std::span<const uint8_t> script) {
    writer_.WriteLE8(static_cast<uint64_t>(value));
    writer_.WriteVarInt(std::ssize(script));
    writer_.WriteBytes(script);
  }

  void SerializeOutputs(const SpendContext& spend, CommitMode commit) {
    switch (commit.OutputsMode()) {
      case CommitOutputs::None:
        // Writes no outputs.
        writer_.WriteVarInt(0);
        return;

      case CommitOutputs::Single: {
        // Writes the current output, and nulls for prior outputs.
        const int output_count = spend.input_index + 1;
        writer_.WriteVarInt(output_count);
        for (int i = 0; i < output_count; ++i) {
          if (i == spend.input_index)  // Serializes the actual output at this index.
            SerializeOutput(spend.tx.Output(i).value, spend.tx.PkScript(i));
          else  // Serializes a null output.
            SerializeOutput(-1, {});
        }
        return;
      }

      case CommitOutputs::All:
        // Writes all outputs.
        writer_.WriteVarInt(spend.tx.OutputCount());
        for (int i = 0; i < spend.tx.OutputCount(); ++i)
          SerializeOutput(spend.tx.Output(i).value, spend.tx.PkScript(i));
        return;
    }
    Assert(false);
  }

  encoding::Writer writer_;
  const std::vector<uint8_t> match_;
};

template <SpendValidationMode kMode>
Hash BuildSpendDigest(const SpendContext&, const lang::Bytes, const lang::Bytes) {
  Assert(false);
  return {};
}

template <>
Hash BuildSpendDigest<SpendValidationMode::Legacy>(const SpendContext& spend, const lang::Bytes sig_arg,
                                                   const lang::Bytes code) {
  Assert(!sig_arg.empty());

  // The commit mode specifies which inputs and outputs we need to commit to.
  const CommitMode commit{sig_arg.back()};

  if (commit.OutputsMode() == CommitOutputs::Single) {
    // We are supposed to commit to the output whose index corresponds to the spending input.
    // However, if that output doesn't exist, legacy behavior requires the spend digest be the 256-bit integer 1.
    if (spend.input_index >= spend.tx.OutputCount()) return {0x01};
  }

  // The serialization determines which information is committed to by the signature.
  const std::vector<uint8_t> serialized = LegacyCommitSerializer{sig_arg}(spend, commit, code);

  // Hash the serialized bytes.
  return crypto::DoubleSha256(serialized);
}

}  // namespace

Hash BuildSpendDigest(const SpendContext& spend, const lang::Bytes sig_arg, const lang::Bytes code) {
  return VisitSpendValidationMode(GetSpendValidationMode(spend.path), [&]<SpendValidationMode kMode> {
    return BuildSpendDigest<kMode>(spend, sig_arg, code);
  });
}

}  // namespace hornet::protocol::script::runtime
