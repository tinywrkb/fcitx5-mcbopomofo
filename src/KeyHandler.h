// Copyright (c) 2022 and onwards The McBopomofo Authors.
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_KEYHANDLER_H_
#define SRC_KEYHANDLER_H_

#include <fcitx/inputmethodengine.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Gramambular.h"
#include "InputState.h"
#include "LanguageModelLoader.h"
#include "Mandarin.h"
#include "McBopomofoLM.h"
#include "UserOverrideModel.h"

namespace McBopomofo {

class KeyHandler {
 public:
  explicit KeyHandler(
      std::shared_ptr<Formosa::Gramambular::LanguageModel> languageModel,
      std::shared_ptr<LanguageModelLoader> languageModelLoader);

  using StateCallback =
      std::function<void(std::unique_ptr<McBopomofo::InputState>)>;
  using ErrorCallback = std::function<void(void)>;

  // Given a fcitx5 KeyEvent and the current state, invokes the stateCallback if
  // a new state is entered, or errorCallback will be invoked. Returns true if
  // the key should be absorbed, signaling that the key is accepted and handled,
  // or false if the event should be let pass through.
  bool handle(fcitx::Key key, McBopomofo::InputState* state,
              const StateCallback& stateCallback,
              const ErrorCallback& errorCallback);

  // Candidate selected. Can assume the context is in a candidate state.
  void candidateSelected(const std::string& candidate,
                         const StateCallback& stateCallback);
  // Candidate panel canceled. Can assume the context is in a candidate state.
  void candidatePanelCancelled(const StateCallback& stateCallback);

  void reset();

  // Sets the Bopomofo keyboard layout.
  void setKeyboardLayout(
      const Formosa::Mandarin::BopomofoKeyboardLayout* layout);

  // Sets select phrase after cursor as candidate.
  void setSelectPhraseAfterCursorAsCandidate(bool flag);

  // Sets move cursor after selection.
  void setMoveCursorAfterSelection(bool flag);

 private:
  bool handleCursorKeys(fcitx::Key key, McBopomofo::InputState* state,
                        const StateCallback& stateCallback,
                        const ErrorCallback& errorCallback);
  bool handleDeleteKeys(fcitx::Key key, McBopomofo::InputState* state,
                        const StateCallback& stateCallback,
                        const ErrorCallback& errorCallback);
  bool handlePunctuation(const std::string& punctuationUnigramKey,
                         const StateCallback& stateCallback,
                         const ErrorCallback& errorCallback);

  // Get the head and the tail of current composed string, separated by the
  // current cursor.
  struct ComposedString {
    std::string head;
    std::string tail;
    // Any tooltip during the build process.
    std::string tooltip;
  };
  ComposedString getComposedString(size_t builderCursor);

  std::unique_ptr<InputStates::Inputting> buildInputtingState();
  std::unique_ptr<InputStates::ChoosingCandidate> buildChoosingCandidateState(
      InputStates::NotEmpty* nonEmptyState);

  // Build a Marking state, ranging from beginCursorIndex to the current builder
  // cursor. It doesn't matter if the beginCursorIndex is behind or after the
  // builder cursor.
  std::unique_ptr<InputStates::Marking> buildMarkingState(
      size_t beginCursorIndex);

  // Returns the text that needs to be evicted from the walked grid due to the
  // grid now being overflown with the recently added reading, then walk the
  // grid.
  std::string popEvictedTextAndWalk();

  // Compute the actual candidate cursor index.
  size_t actualCandidateCursorIndex();

  // Pin a node with a fixed unigram value, usually a candidate.
  void pinNode(const std::string& candidate);

  void walk();

  std::shared_ptr<Formosa::Gramambular::LanguageModel> languageModel_;
  std::shared_ptr<LanguageModelLoader> languageModelLoader_;

  UserOverrideModel userOverrideModel_;
  Formosa::Mandarin::BopomofoReadingBuffer reading_;
  std::unique_ptr<Formosa::Gramambular::BlockReadingBuilder> builder_;

  // latest walked path (trellis) using the Viterbi algorithm
  std::vector<Formosa::Gramambular::NodeAnchor> walkedNodes_;

  bool selectPhraseAfterCursorAsCandidate_;
  bool moveCursorAfterSelection_;
};

}  // namespace McBopomofo

#endif  // SRC_KEYHANDLER_H_
