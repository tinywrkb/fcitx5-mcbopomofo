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

#ifndef MCBOPOMOFOLM_H
#define MCBOPOMOFOLM_H

// #include "AssociatedPhrases.h"
#include "ParselessLM.h"
#include "PhraseReplacementMap.h"
#include "UserPhrasesLM.h"
#include <stdio.h>
#include <unordered_set>
#include <functional>

namespace McBopomofo {

/// McBopomofoLM is a facade for managing a set of models including
/// the input method language model, user phrases and excluded phrases.
///
/// It is the primary model class that the input controller and grammar builder
/// of McBopomofo talks to. When the grammar builder starts to build a sentence
/// from a series of BPMF readings, it passes the readings to the model to see
/// if there are valid unigrams, and use returned unigrams to produce the final
/// results.
///
/// McBopomofoLM combine and transform the unigrams from the primary language
/// model and user phrases. The process is
///
/// 1) Get the original unigrams.
/// 2) Drop the unigrams whose value is contained in the exclusion map.
/// 3) Replace the values of the unigrams using the phrase replacement map.
/// 4) Replace the values of the unigrams using an external converter lambda.
/// 5) Drop the duplicated phrases.
///
/// The controller can ask the model to load the primary input method language
/// model while launching and to load the user phrases anytime if the custom
/// files are modified. It does not keep the reference of the data pathes but
/// you have to pass the paths when you ask it to do loading.
class McBopomofoLM : public Formosa::Gramambular::LanguageModel {
public:
    McBopomofoLM();
    ~McBopomofoLM();

    /// Asks to load the primary language model at the given path.
    /// @param languageModelPath The path of the language model.
    void loadLanguageModel(const char* languageModelPath);
    /// If the data model is already loaded.
    bool isDataModelLoaded();

    // /// Asks to load the associated phrases at the given path.
    // /// @param associatedPhrasesPath The path of the associated phrases.
    // void loadAssociatedPhrases(const char* associatedPhrasesPath);
    // /// If the associated phrases already loaded.
    // bool isAssociatedPhrasesLoaded();

    /// Asks to load the user phrases and excluded phrases at the given path.
    /// @param userPhrasesPath The path of user phrases.
    /// @param excludedPhrasesPath The path of excluded phrases.
    void loadUserPhrases(const char* userPhrasesPath, const char* excludedPhrasesPath);
    /// Asks to load th phrase replacement table at the given path.
    /// @param phraseReplacementPath The path of the phrase replacement table.
    void loadPhraseReplacementMap(const char* phraseReplacementPath);

    /// Not implemented since we do not have data to provide bigram function.
    const std::vector<Formosa::Gramambular::Bigram> bigramsForKeys(const std::string& preceedingKey, const std::string& key);
    /// Returns a list of available unigram for the given key.
    /// @param key A string represents the BPMF reading or a symbol key. For
    ///     example, it you pass "ㄇㄚ", it returns "嗎", "媽", and so on.
    const std::vector<Formosa::Gramambular::Unigram> unigramsForKey(const std::string& key);
    /// If the model has unigrams for the given key.
    /// @param key The key.
    bool hasUnigramsForKey(const std::string& key);

    /// Enables or disables phrase replacement.
    void setPhraseReplacementEnabled(bool enabled);
    /// If phrase replacement is enabled or not.
    bool phraseReplacementEnabled();

    /// Enables or disables the external converter.
    void setExternalConverterEnabled(bool enabled);
    /// If the external converted is enabled or not.
    bool externalConverterEnabled();
    /// Sets a lambda to let the values of unigrams could be converted by it.
    void setExternalConverter(std::function<std::string(std::string)> externalConverter);

    const std::vector<std::string> associatedPhrasesForKey(const std::string& key);
    bool hasAssociatedPhrasesForKey(const std::string& key);

protected:
    /// Filters and converts the input unigrams and return a new list of unigrams.
    ///
    /// @param unigrams The unigrams to be processed.
    /// @param excludedValues The values to excluded unigrams.
    /// @param insertedValues The values for unigrams already in the results.
    ///   It helps to prevent duplicated unigrams. Please note that the method
    ///   has a side effect that it inserts values to `insertedValues`.
    const std::vector<Formosa::Gramambular::Unigram> filterAndTransformUnigrams(const std::vector<Formosa::Gramambular::Unigram> unigrams,
        const std::unordered_set<std::string>& excludedValues,
        std::unordered_set<std::string>& insertedValues);

    ParselessLM m_languageModel;
    UserPhrasesLM m_userPhrases;
    UserPhrasesLM m_excludedPhrases;
    PhraseReplacementMap m_phraseReplacement;
    // AssociatedPhrases m_associatedPhrases;
    bool m_phraseReplacementEnabled;
    bool m_externalConverterEnabled;
    std::function<std::string(std::string)> m_externalConverter;
};
};

#endif
