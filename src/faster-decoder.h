// faster-decoder.h

// Copyright 2009-2011  Microsoft Corporation
//                2013  Johns Hopkins University (author: Daniel Povey)
//                2018  Binbin Zhang

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#ifndef FASTER_DECODER_H_
#define FASTER_DECODER_H_

#include <vector>
#include <queue>
#include <string>
#include <limits>

#include "utils.h"
#include "tree.h"
#include "hash-list.h"
#include "fst.h"
#include "decodable.h"
#include "object-pool.h"

namespace xdecoder {

struct FasterDecoderOptions {
  float beam;
  int32_t max_active;
  int32_t min_active;
  float beam_delta;
  float hash_ratio;
  FasterDecoderOptions(): beam(16.0),
                          max_active(std::numeric_limits<int32_t>::max()),
                          min_active(20),   // This decoder mostly used for
                                            // alignment, use small default.
                          beam_delta(0.5),
                          hash_ratio(2.0) { }
};

class FasterDecoder {
 public:
  FasterDecoder(const Fst& fst,
                const FasterDecoderOptions& config);

  void SetOptions(const FasterDecoderOptions &config) { config_ = config; }

  ~FasterDecoder() {
    ClearToks(toks_.Clear());
    delete token_pool_;
  }

  void Decode(Decodable *decodable);

  /// Returns true if a final state was active on the last frame.
  bool ReachedFinal();

  /// GetBestPath gets the decoding traceback. If "use_final_probs" is true
  /// AND we reached a final state, it limits itself to final states;
  /// otherwise it gets the most likely token not taking into account
  /// final-probs. Returns true if the output best path was not the empty
  bool GetBestPath(std::vector<int32_t> *results,
                   bool use_final_probs = true);

  /// As a new alternative to Decode(), you can call InitDecoding
  /// and then (possibly multiple times) AdvanceDecoding().
  void InitDecoding();


  /// This will decode until there are no more frames ready in the decodable
  /// object, but if max_num_frames is >= 0 it will decode no more than
  /// that many frames.
  void AdvanceDecoding(Decodable *decodable,
                       int32_t max_num_frames = -1);

  /// Returns the number of frames already decoded.
  int32_t NumFramesDecoded() const { return num_frames_decoded_; }

 protected:
  class Token {
   public:
    Arc arc_;  // contains only the graph part of the cost;
    // we can work out the acoustic part from difference between
    // "cost_" and prev->cost_.
    Token *prev_;
    int32_t ref_count_;
    // if you are looking for weight_ here, it was removed and now we just have
    // cost_, which corresponds to ConvertToCost(weight_).
    double cost_;
    inline Token(): prev_(NULL), ref_count_(1) {}
    inline void Init(const Arc &arc, float ac_cost, Token *prev) {
      arc_ = arc;
      prev_ = prev;
      ref_count_ = 1;
      if (prev) {
        prev->ref_count_++;
        cost_ = prev->cost_ + arc.weight + ac_cost;
      } else {
        cost_ = arc.weight + ac_cost;
      }
    }
    inline Token(const Arc &arc, float ac_cost, Token *prev) {
      Init(arc, ac_cost, prev);
    }
    inline void Init(const Arc &arc, Token *prev) {
      arc_ = arc;
      prev_ = prev;
      ref_count_ = 1;
      if (prev) {
        prev->ref_count_++;
        cost_ = prev->cost_ + arc.weight;
      } else {
        cost_ = arc.weight;
      }
    }
    inline Token(const Arc &arc, Token *prev) {
      Init(arc, prev);
    }
    inline bool operator < (const Token &other) {
      return cost_ > other.cost_;
    }

    inline static void TokenDelete(Token *tok, IObjectPool<Token> *token_pool) {
      while (--tok->ref_count_ == 0) {
        Token *prev = tok->prev_;
        token_pool->Delete(tok);
        if (prev == NULL) return;
        else
          tok = prev;
      }
      CHECK(tok->ref_count_ > 0);
    }
  };
  typedef HashList<int32_t, Token*>::Elem Elem;


  /// Gets the weight cutoff.  Also counts the active tokens.
  double GetCutoff(Elem *list_head, size_t *tok_count,
                   float *adaptive_beam, Elem **best_elem);

  void PossiblyResizeHash(size_t num_toks);

  // ProcessEmitting returns the likelihood cutoff used.
  // It decodes the frame num_frames_decoded_ of the decodable object
  // and then increments num_frames_decoded_
  double ProcessEmitting(Decodable *decodable);

  // TODO(Binbin Zhang) first time we go through this,
  // could avoid using the queue.
  void ProcessNonemitting(double cutoff);

  // HashList defined in ../util/hash-list.h.  It actually allows us to maintain
  // more than one list (e.g. for current and previous frames), but only one of
  // them at a time can be indexed by int32_t.
  HashList<int32_t, Token*> toks_;
  const Fst& fst_;
  FasterDecoderOptions config_;
  std::vector<int32_t> queue_;  // temp variable used in ProcessNonemitting,
  std::vector<float> tmp_array_;  // used in GetCutoff.
  // make it class member to avoid internal new/delete.

  // Keep track of the number of frames decoded in the current file.
  int32_t num_frames_decoded_;

  // Token pool
  IObjectPool<Token> *token_pool_;

  // It might seem unclear why we call ClearToks(toks_.Clear()).
  // There are two separate cleanup tasks we need to do at when we start a
  // new file. one is to delete the Token objects in the list; the other is
  // to delete the Elem objects.  toks_.Clear() just clears them from the hash
  // and gives ownership to the caller, who then has to call toks_.Delete(e)
  // for each one.  It was designed this way for convenience in propagating
  // tokens from one frame to the next.
  void ClearToks(Elem *list);

 private:
  DISALLOW_COPY_AND_ASSIGN(FasterDecoder);
};


}  // end namespace xdecoder


#endif
