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

#include "KeyHandler.h"

#include <fcitx-utils/i18n.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <utility>

#include "UTF8Helper.h"

namespace McBopomofo {

constexpr char kJoinSeparator[] = "-";
constexpr char kPunctuationListKey[] = "_punctuation_list";
constexpr char kPunctuationKeyPrefix[] = "_punctuation_";
constexpr size_t kMinValidMarkingReadingCount = 2;
constexpr size_t kMaxValidMarkingReadingCount = 6;

constexpr int kUserOverrideModelCapacity = 500;
constexpr double kObservedOverrideHalfLife = 5400.0;  // 1.5 hr.
// Unigram whose score is below this shouldn't be put into user override model.
constexpr double kNoOverrideThreshold = -8.0;
constexpr double kEpsilon = 0.000001;

// Maximum composing buffer size, roughly in codepoints.
// TODO(unassigned): maybe make this configurable.
constexpr size_t kComposingBufferSize = 10;

static const char* GetKeyboardLayoutName(
    const Formosa::Mandarin::BopomofoKeyboardLayout* layout) {
  if (layout == Formosa::Mandarin::BopomofoKeyboardLayout::ETenLayout()) {
    return "ETen";
  } else if (layout == Formosa::Mandarin::BopomofoKeyboardLayout::HsuLayout()) {
    return "Hsu";
  } else if (layout ==
             Formosa::Mandarin::BopomofoKeyboardLayout::ETen26Layout()) {
    return "ETen26";
  } else if (layout ==
             Formosa::Mandarin::BopomofoKeyboardLayout::HanyuPinyinLayout()) {
    return "HanyuPinyin";
  } else if (layout == Formosa::Mandarin::BopomofoKeyboardLayout::IBMLayout()) {
    return "IBM";
  }
  return "Standard";
}

static bool MarkedPhraseExists(Formosa::Gramambular::LanguageModel* lm,
                               const std::string& reading,
                               const std::string& value) {
  if (!lm->hasUnigramsForKey(reading)) {
    return false;
  }

  auto unigrams = lm->unigramsForKey(reading);
  return std::any_of(unigrams.begin(), unigrams.end(),
                     [&value](const auto& unigram) {
                       return unigram.keyValue.value == value;
                     });
}

static double GetEpochNowInSeconds() {
  auto now = std::chrono::system_clock::now();
  int64_t timestamp = std::chrono::time_point_cast<std::chrono::seconds>(now)
                          .time_since_epoch()
                          .count();
  return timestamp;
}

static double FindHighestScore(
    const std::vector<Formosa::Gramambular::NodeAnchor>& nodeAnchors,
    double epsilon) {
  double highestScore = 0.0;
  for (const auto& anchor : nodeAnchors) {
    double score = anchor.node->highestUnigramScore();
    if (score > highestScore) {
      highestScore = score;
    }
  }
  return highestScore + epsilon;
}

KeyHandler::KeyHandler(
    std::shared_ptr<Formosa::Gramambular::LanguageModel> languageModel,
    std::shared_ptr<LanguageModelLoader> languageModelLoader)
    : languageModel_(std::move(languageModel)),
      languageModelLoader_(std::move(languageModelLoader)),
      userOverrideModel_(kUserOverrideModelCapacity, kObservedOverrideHalfLife),
      reading_(Formosa::Mandarin::BopomofoKeyboardLayout::StandardLayout()) {
  builder_ = std::make_unique<Formosa::Gramambular::BlockReadingBuilder>(
      languageModel_.get());
  builder_->setJoinSeparator(kJoinSeparator);
}

bool KeyHandler::handle(fcitx::Key key, McBopomofo::InputState* state,
                        const StateCallback& stateCallback,
                        const ErrorCallback& errorCallback) {
  // key.isSimple() is true => key.sym() guaranteed to be printable ASCII.
  char asciiChar = key.isSimple() ? key.sym() : 0;

  // See if it's valid BPMF reading.
  if (reading_.isValidKey(asciiChar)) {
    reading_.combineKey(asciiChar);

    // If asciiChar does not lead to a tone marker, we are done. Tone marker
    // would lead to composing of the reading, which is handled after this.
    if (!reading_.hasToneMarker()) {
      stateCallback(buildInputtingState());
      return true;
    }
  }

  // Compose the reading if either there's a tone marker, or if the reading is
  // not empty, and space is pressed.
  bool shouldComposeReading =
      reading_.hasToneMarker() ||
      (!reading_.isEmpty() && key.check(FcitxKey_space));

  if (shouldComposeReading) {
    std::string syllable = reading_.syllable().composedString();
    reading_.clear();

    if (!languageModel_->hasUnigramsForKey(syllable)) {
      errorCallback();
      stateCallback(buildInputtingState());
      return true;
    }

    builder_->insertReadingAtCursor(syllable);
    std::string evictedText = popEvictedTextAndWalk();

    std::string overrideValue = userOverrideModel_.suggest(
        walkedNodes_, builder_->cursorIndex(), GetEpochNowInSeconds());
    if (!overrideValue.empty()) {
      size_t cursorIndex = actualCandidateCursorIndex();
      std::vector<Formosa::Gramambular::NodeAnchor> nodes =
          builder_->grid().nodesCrossingOrEndingAt(cursorIndex);
      double highestScore = FindHighestScore(nodes, kEpsilon);
      builder_->grid().overrideNodeScoreForSelectedCandidate(
          cursorIndex, overrideValue, static_cast<float>(highestScore));
    }

    auto inputtingState = buildInputtingState();
    inputtingState->evictedText = evictedText;
    stateCallback(std::move(inputtingState));
    return true;
  }

  // Space hit: see if we should enter the candidate choosing state.
  auto maybeNotEmptyState = dynamic_cast<InputStates::NotEmpty*>(state);
  if (key.check(FcitxKey_space) && maybeNotEmptyState != nullptr &&
      reading_.isEmpty()) {
    stateCallback(buildChoosingCandidateState(maybeNotEmptyState));
    return true;
  }

  // Esc hit.
  if (key.check(FcitxKey_Escape)) {
    if (maybeNotEmptyState == nullptr) {
      return false;
    }

    if (!reading_.isEmpty()) {
      reading_.clear();
      if (!builder_->length()) {
        stateCallback(std::make_unique<InputStates::Empty>());
      } else {
        stateCallback(buildInputtingState());
      }
    } else {
      stateCallback(buildInputtingState());
    }
    return true;
  }

  // Cursor keys.
  auto shift = fcitx::KeyStates{fcitx::KeyState::Shift};
  static std::array<fcitx::Key, 8> cursorKeys{
      fcitx::Key(FcitxKey_Left),        fcitx::Key(FcitxKey_Right),
      fcitx::Key(FcitxKey_Home),        fcitx::Key(FcitxKey_End),
      fcitx::Key(FcitxKey_Left, shift), fcitx::Key(FcitxKey_Right, shift),
      fcitx::Key(FcitxKey_Home, shift), fcitx::Key(FcitxKey_End, shift)};
  if (key.checkKeyList(cursorKeys)) {
    return handleCursorKeys(key, state, stateCallback, errorCallback);
  }

  // Backspace and Del.
  static std::array<fcitx::Key, 2> deleteKeys{fcitx::Key(FcitxKey_BackSpace),
                                              fcitx::Key(FcitxKey_Delete)};
  if (key.checkKeyList(deleteKeys)) {
    return handleDeleteKeys(key, state, stateCallback, errorCallback);
  }

  // Enter.
  if (key.check(FcitxKey_Return)) {
    if (maybeNotEmptyState == nullptr) {
      return false;
    }

    if (!reading_.isEmpty()) {
      errorCallback();
      stateCallback(buildInputtingState());
      return true;
    }

    // See if we are in Marking state, and, if a valid mark, accept it.
    if (auto marking = dynamic_cast<InputStates::Marking*>(state)) {
      if (marking->acceptable) {
        languageModelLoader_->addUserPhrase(marking->reading,
                                            marking->markedText);
        stateCallback(buildInputtingState());
      } else {
        errorCallback();
        stateCallback(buildMarkingState(marking->markStartGridCursorIndex));
      }
      return true;
    }

    auto inputtingState = buildInputtingState();
    // Steal the composingBuffer built by the inputting state.
    auto committingState = std::make_unique<InputStates::Committing>(
        inputtingState->composingBuffer);
    stateCallback(std::move(committingState));
    reset();
    return true;
  }

  // Punctuation key: backtick or grave accent.
  if (key.check(FcitxKey_grave) &&
      languageModel_->hasUnigramsForKey(kPunctuationListKey)) {
    if (reading_.isEmpty()) {
      builder_->insertReadingAtCursor(kPunctuationListKey);

      std::string evictedText = popEvictedTextAndWalk();

      auto inputtingState = buildInputtingState();
      inputtingState->evictedText = evictedText;
      auto choosingCanidateState =
          buildChoosingCandidateState(inputtingState.get());
      stateCallback(std::move(inputtingState));
      stateCallback(std::move(choosingCanidateState));
    } else {
      // Punctuation ignored if a bopomofo reading is active..
      errorCallback();
    }
    return true;
  }

  if (asciiChar != 0) {
    std::string chrStr(1, asciiChar);

    // Bopomofo layout-specific punctuation handling.
    std::string unigram = std::string(kPunctuationKeyPrefix) +
                          GetKeyboardLayoutName(reading_.keyboardLayout()) +
                          "_" + chrStr;
    if (handlePunctuation(unigram, stateCallback, errorCallback)) {
      return true;
    }

    // Not handled, try generic punctuations.
    unigram = std::string(kPunctuationKeyPrefix) + chrStr;
    if (handlePunctuation(unigram, stateCallback, errorCallback)) {
      return true;
    }
  }

  // No key is handled. Refresh and consume the key.
  if (maybeNotEmptyState != nullptr) {
    errorCallback();
    stateCallback(buildInputtingState());
    return true;
  }

  return false;
}

void KeyHandler::candidateSelected(const std::string& candidate,
                                   const StateCallback& stateCallback) {
  pinNode(candidate);
  stateCallback(buildInputtingState());
}

void KeyHandler::candidatePanelCancelled(const StateCallback& stateCallback) {
  stateCallback(buildInputtingState());
}

void KeyHandler::reset() {
  reading_.clear();
  builder_->clear();
  walkedNodes_.clear();
}

void KeyHandler::setKeyboardLayout(
    const Formosa::Mandarin::BopomofoKeyboardLayout* layout) {
  reading_.setKeyboardLayout(layout);
}

void KeyHandler::setSelectPhraseAfterCursorAsCandidate(bool flag) {
  selectPhraseAfterCursorAsCandidate_ = flag;
}

void KeyHandler::setMoveCursorAfterSelection(bool flag) {
  moveCursorAfterSelection_ = flag;
}

bool KeyHandler::handleCursorKeys(fcitx::Key key, McBopomofo::InputState* state,
                                  const StateCallback& stateCallback,
                                  const ErrorCallback& errorCallback) {
  if (dynamic_cast<InputStates::Inputting*>(state) == nullptr &&
      dynamic_cast<InputStates::Marking*>(state) == nullptr) {
    return false;
  }
  size_t markBeginCursorIndex = builder_->cursorIndex();
  auto marking = dynamic_cast<InputStates::Marking*>(state);
  if (marking != nullptr) {
    markBeginCursorIndex = marking->markStartGridCursorIndex;
  }

  if (!reading_.isEmpty()) {
    errorCallback();
    stateCallback(buildInputtingState());
    return true;
  }

  bool isValidMove = false;
  switch (key.sym()) {
    case FcitxKey_Left:
      if (builder_->cursorIndex() > 0) {
        builder_->setCursorIndex(builder_->cursorIndex() - 1);
        isValidMove = true;
      }
      break;
    case FcitxKey_Right:
      if (builder_->cursorIndex() < builder_->length()) {
        builder_->setCursorIndex(builder_->cursorIndex() + 1);
        isValidMove = true;
      }
      break;
    case FcitxKey_Home:
      builder_->setCursorIndex(0);
      isValidMove = true;
      break;
    case FcitxKey_End:
      builder_->setCursorIndex(builder_->length());
      isValidMove = true;
      break;
    default:
      // Ignored.
      break;
  }

  if (!isValidMove) {
    errorCallback();
  }

  if (key.states() & fcitx::KeyState::Shift &&
      builder_->cursorIndex() != markBeginCursorIndex) {
    stateCallback(buildMarkingState(markBeginCursorIndex));
  } else {
    stateCallback(buildInputtingState());
  }
  return true;
}

bool KeyHandler::handleDeleteKeys(fcitx::Key key, McBopomofo::InputState* state,
                                  const StateCallback& stateCallback,
                                  const ErrorCallback& errorCallback) {
  if (dynamic_cast<InputStates::NotEmpty*>(state) == nullptr) {
    return false;
  }

  if (reading_.isEmpty()) {
    bool isValidDelete = false;

    if (key.check(FcitxKey_BackSpace) && builder_->cursorIndex() > 0) {
      builder_->deleteReadingBeforeCursor();
      isValidDelete = true;
    } else if (key.check(FcitxKey_Delete) &&
               builder_->cursorIndex() < builder_->length()) {
      builder_->deleteReadingAfterCursor();
      isValidDelete = true;
    }
    if (!isValidDelete) {
      errorCallback();
      stateCallback(buildInputtingState());
      return true;
    }
    walk();
  } else {
    if (key.check(FcitxKey_BackSpace)) {
      reading_.backspace();
    } else {
      // Del not supported when bopomofo reading is active.
      errorCallback();
    }
  }

  if (reading_.isEmpty() && builder_->length() == 0) {
    // Cancel the previous input state if everything is empty now.
    stateCallback(std::make_unique<InputStates::EmptyIgnoringPrevious>());
  } else {
    stateCallback(buildInputtingState());
  }
  return true;
}

bool KeyHandler::handlePunctuation(const std::string& punctuationUnigramKey,
                                   const StateCallback& stateCallback,
                                   const ErrorCallback& errorCallback) {
  if (!languageModel_->hasUnigramsForKey(punctuationUnigramKey)) {
    return false;
  }

  if (!reading_.isEmpty()) {
    errorCallback();
    stateCallback(buildInputtingState());
    return true;
  }

  builder_->insertReadingAtCursor(punctuationUnigramKey);
  std::string evictedText = popEvictedTextAndWalk();

  auto inputtingState = buildInputtingState();
  inputtingState->evictedText = evictedText;
  stateCallback(std::move(inputtingState));
  return true;
}

KeyHandler::ComposedString KeyHandler::getComposedString(size_t builderCursor) {
  // To construct an Inputting state, we need to first retrieve the entire
  // composing buffer from the current grid, then split the composed string into
  // head and tail, so that we can insert the current reading (if not-empty)
  // between them.
  //
  // We'll also need to compute the UTF-8 cursor index. The idea here is we use
  // a "running" index that will eventually catch the cursor index in the
  // builder. The tricky part is that if the spanning length of the node that
  // the cursor is at does not agree with the actual codepoint count of the
  // node's value, we'll need to move the cursor at the end of the node to avoid
  // confusions.

  size_t runningCursor = 0;  // spanning-length-based, like the builder cursor

  std::string composed;
  size_t composedCursor =
      0;  // UTF-8 (so "byte") cursor per fcitx5 requirement.

  std::string tooltip;

  for (const Formosa::Gramambular::NodeAnchor& anchor : walkedNodes_) {
    const Formosa::Gramambular::Node* node = anchor.node;
    if (node == nullptr) {
      continue;
    }

    const std::string& value = node->currentKeyValue().value;
    composed += value;

    // No work if runningCursor has already caught up with builderCursor.
    if (runningCursor == builderCursor) {
      continue;
    }
    size_t spanningLength = anchor.spanningLength;

    // Simple case: if the running cursor is behind, add the spanning length.
    if (runningCursor + spanningLength <= builderCursor) {
      composedCursor += value.length();
      runningCursor += spanningLength;
      continue;
    }

    // The builder cursor is in the middle of the node.
    size_t distance = builderCursor - runningCursor;
    std::u32string u32Value = ToU32(value);

    // The actual partial value's code point length is the shorter of the
    // distance and the value's code point count.
    size_t cpLen = std::min(distance, u32Value.length());
    std::u32string actualU32Value(u32Value.begin(), u32Value.begin() + cpLen);
    std::string actualValue = ToU8(actualU32Value);
    composedCursor += actualValue.length();
    runningCursor += distance;

    // Create a tooltip to warn the user that their cursor is between two
    // readings (syllables) even if the cursor is not in the middle of a
    // composed string due to its being shorter than the number of readings.
    if (u32Value.length() < spanningLength) {
      // builderCursor is guaranteed to be > 0. If it was 0, we wouldn't even
      // reach here due to runningCursor having already "caught up" with
      // builderCursor. It is also guaranteed to be less than the size of the
      // builder's readings for the same reason: runningCursor would have
      // already caught up.
      const std::string& prevReading = builder_->readings()[builderCursor - 1];
      const std::string& nextReading = builder_->readings()[builderCursor];

      tooltip = fmt::format(_("Cursor is between syllables {0} and {1}"),
                            prevReading, nextReading);
    }
  }

  std::string head = composed.substr(0, composedCursor);
  std::string tail =
      composed.substr(composedCursor, composed.length() - composedCursor);
  return KeyHandler::ComposedString{
      .head = head, .tail = tail, .tooltip = tooltip};
}

std::unique_ptr<InputStates::Inputting> KeyHandler::buildInputtingState() {
  auto composedString = getComposedString(builder_->cursorIndex());

  std::string head = composedString.head;
  std::string reading = reading_.composedString();
  std::string tail = composedString.tail;

  std::string composingBuffer = head + reading + tail;
  size_t cursorIndex = head.length() + reading.length();
  return std::make_unique<InputStates::Inputting>(composingBuffer, cursorIndex,
                                                  composedString.tooltip);
}

std::unique_ptr<InputStates::ChoosingCandidate>
KeyHandler::buildChoosingCandidateState(InputStates::NotEmpty* nonEmptyState) {
  std::vector<Formosa::Gramambular::NodeAnchor> anchoredNodes =
      builder_->grid().nodesCrossingOrEndingAt(actualCandidateCursorIndex());

  // sort the nodes, so that longer nodes (representing longer phrases) are
  // placed at the top of the candidate list
  stable_sort(anchoredNodes.begin(), anchoredNodes.end(),
              [](const Formosa::Gramambular::NodeAnchor& a,
                 const Formosa::Gramambular::NodeAnchor& b) {
                return a.node->key().length() > b.node->key().length();
              });

  std::vector<std::string> candidates;
  for (const Formosa::Gramambular::NodeAnchor& anchor : anchoredNodes) {
    const std::vector<Formosa::Gramambular::KeyValuePair>& nodeCandidates =
        anchor.node->candidates();
    for (const Formosa::Gramambular::KeyValuePair& kv : nodeCandidates) {
      candidates.push_back(kv.value);
    }
  }

  return std::make_unique<InputStates::ChoosingCandidate>(
      nonEmptyState->composingBuffer, nonEmptyState->cursorIndex, candidates);
}

std::unique_ptr<InputStates::Marking> KeyHandler::buildMarkingState(
    size_t beginCursorIndex) {
  // We simply build two composed strings and use the delta between the shorter
  // and the longer one as the marked text.
  ComposedString from = getComposedString(beginCursorIndex);
  ComposedString to = getComposedString(builder_->cursorIndex());
  size_t composedStringCursorIndex = to.head.length();
  std::string composed = to.head + to.tail;
  size_t fromIndex = beginCursorIndex;
  size_t toIndex = builder_->cursorIndex();

  if (beginCursorIndex > builder_->cursorIndex()) {
    std::swap(from, to);
    std::swap(fromIndex, toIndex);
  }

  // Now from is shorter and to is longer. The marked text is just the delta.
  std::string head = from.head;
  std::string marked =
      std::string(to.head.begin() + from.head.length(), to.head.end());
  std::string tail = to.tail;

  // Collect the readings.
  auto readingBegin = builder_->readings().begin();
  std::vector<std::string> readings(readingBegin + fromIndex,
                                    readingBegin + toIndex);
  std::string readingUiText;  // What the user sees.
  std::string readingValue;   // What is used for adding a user phrase.
  for (auto it = readings.begin(); it != readings.end(); ++it) {
    readingValue += *it;
    readingUiText += *it;
    if (it + 1 != readings.end()) {
      readingValue += kJoinSeparator;
      readingUiText += " ";
    }
  }

  bool isValid = false;
  std::string status;
  // Validate the marking.
  if (readings.size() < kMinValidMarkingReadingCount) {
    status = fmt::format(_("{0} syllables required"),
                         std::to_string(kMinValidMarkingReadingCount));
  } else if (readings.size() > kMaxValidMarkingReadingCount) {
    status = fmt::format(_("{0} syllables maximum"),
                         std::to_string(kMaxValidMarkingReadingCount));
  } else if (MarkedPhraseExists(languageModel_.get(), readingValue, marked)) {
    status = _("phrase already exists");
  } else {
    status = _("press Enter to add the phrase");
    isValid = true;
  }

  std::string tooltip = fmt::format(_("Marked: {0}, syllables: {1}, {2}"),
                                    marked, readingUiText, status);

  return std::make_unique<InputStates::Marking>(
      composed, composedStringCursorIndex, tooltip, beginCursorIndex, head,
      marked, tail, readingValue, isValid);
}

size_t KeyHandler::actualCandidateCursorIndex() {
  size_t cursorIndex = builder_->cursorIndex();
  if (selectPhraseAfterCursorAsCandidate_) {
    if (cursorIndex < builder_->length()) {
      ++cursorIndex;
    }
  } else {
    // Cursor must be in the middle or right after a node. So if the cursor is
    // at the beginning, move by one.
    if (!cursorIndex && builder_->length() > 0) {
      ++cursorIndex;
    }
  }
  return cursorIndex;
}

std::string KeyHandler::popEvictedTextAndWalk() {
  // in an ideal world, we can as well let the user type forever,
  // but because the Viterbi algorithm has a complexity of O(N^2),
  // the walk will become slower as the number of nodes increase,
  // therefore we need to "pop out" overflown text -- they usually
  // lose their influence over the whole MLE anyway -- so that when
  // the user type along, the already composed text at front will
  // be popped out
  // TODO(unassigned): Is the algorithm really O(n^2)? Audit.
  std::string evictedText;
  if (builder_->grid().width() > kComposingBufferSize &&
      !walkedNodes_.empty()) {
    Formosa::Gramambular::NodeAnchor& anchor = walkedNodes_[0];
    evictedText = anchor.node->currentKeyValue().value;
    builder_->removeHeadReadings(anchor.spanningLength);
  }

  walk();
  return evictedText;
}

void KeyHandler::pinNode(const std::string& candidate) {
  size_t cursorIndex = actualCandidateCursorIndex();
  Formosa::Gramambular::NodeAnchor selectedNode =
      builder_->grid().fixNodeSelectedCandidate(cursorIndex, candidate);
  double score = selectedNode.node->scoreForCandidate(candidate);
  if (score > kNoOverrideThreshold) {
    userOverrideModel_.observe(walkedNodes_, cursorIndex, candidate,
                               GetEpochNowInSeconds());
  }

  walk();

  if (moveCursorAfterSelection_) {
    size_t nextPosition = 0;
    for (auto node : walkedNodes_) {
      if (nextPosition >= cursorIndex) {
        break;
      }
      nextPosition += node.spanningLength;
    }
    if (nextPosition <= builder_->length()) {
      builder_->setCursorIndex(nextPosition);
    }
  }
}

void KeyHandler::walk() {
  // retrieve the most likely trellis, i.e. a Maximum Likelihood Estimation
  // of the best possible Mandarin characters given the input syllables,
  // using the Viterbi algorithm implemented in the Gramambular library.
  Formosa::Gramambular::Walker walker(&builder_->grid());

  // the reverse walk traces the trellis from the end
  walkedNodes_ = walker.reverseWalk(builder_->grid().width());

  // then we reverse the nodes so that we get the forward-walked nodes
  std::reverse(walkedNodes_.begin(), walkedNodes_.end());
}

}  // namespace McBopomofo
