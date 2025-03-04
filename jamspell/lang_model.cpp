#include <iostream>
#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <ostream>
#include <cstring>
#include <algorithm>
#include "lang_model.hpp"

#include <contrib/cityhash/city.h>

#ifndef ssize_t
#define ssize_t int
#endif

namespace NJamSpell {

class MemStream: public std::basic_streambuf<char> {
public:
    MemStream(char* buff, long maxSize)
        : Buff(buff)
        , MaxSize(maxSize)
        , Pos(0)
    {
    }

	std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (n <= 0) {
            return n;
        }
        long toCopy = std::min<long>(n, MaxSize - Pos);
        memcpy(Buff + Pos, s, toCopy);
        Pos += toCopy;
        return n;
    }
    void Reset() {
        Pos = 0;
    }
    long Size() const {
        return Pos;
    }
private:
    char* Buff;
    long MaxSize;
    long Pos;
};

template<typename T>
std::string DumpKey(const T& key) {
    std::stringbuf buf;
    std::ostream out(&buf);
    NHandyPack::Dump(out, key);
    return buf.str();
}

template<typename T>
void PrepareNgramKeys(const T& grams, std::vector<std::string>& keys) {
    for (auto&& it: grams) {
        keys.push_back(DumpKey(it.first));
    }
}

template<typename T>
int RemoveLowFreqNgramKeys(T& grams, const int& minWordFreq) {
    int numRemoved = 0;
    for( auto it = grams.begin(); it != grams.end(); ) {
        if (it->second < minWordFreq ) {
	    it = grams.erase(it);
	    numRemoved++;
	} else {
	    ++it;
	}
    }
    return numRemoved;
}

static const uint32_t MAX_REAL_NUM = 268435456;
static const uint32_t MAX_AVAILABLE_NUM = 65536;

uint16_t PackInt32(uint32_t num) {
    double r = double(num) / double(MAX_REAL_NUM);
    assert(r >= 0.0 && r <= 1.0);
    r = pow(r, 0.2);
    r *= MAX_AVAILABLE_NUM;
    return uint16_t(r);
}

uint32_t UnpackInt32(uint16_t num) {
    double r = double(num) / double(MAX_AVAILABLE_NUM);
    r = pow(r, 5.0);
    r *= MAX_REAL_NUM;
    return uint32_t(ceil(r));
}

template<typename T>
void InitializeBuckets(const T& grams, TPerfectHash& ph, std::vector<std::pair<uint16_t, uint16_t>>& buckets) {
    for (auto&& it: grams) {
        std::string key = DumpKey(it.first);
        uint32_t bucket = ph.Hash(key);
        if (bucket >= buckets.size()) {
            std::cerr << bucket << " " << buckets.size() << "\n";
        }
        assert(bucket < buckets.size());
        std::pair<uint16_t, uint16_t> data;
        data.first = CityHash16(key);
        data.second = PackInt32(it.second);
        buckets[bucket] = data;
    }
}

void TLangModel::RemoveLowFreqWord(const std::unordered_map<TGram1Key, TCount>& grams1, const int& minWordFreq) {
    std::cerr << "[info] cleaning word with frequency less than " << minWordFreq << " from vocab" << std::endl;
    std::cerr << "[info] vocab size " << WordToId.size() << " before cleaning" << std::endl;
    std::vector<std::wstring> wordsToRemove;
    
    for(auto&& it: grams1) {
        if (it.second < minWordFreq ) {
	    TWordId wid = it.first;
	    TWord word = GetWordById(wid);
	    if (!word.Ptr || !word.Len) {
		std::cerr << "error: word ID [" << wid << "] is not found" << std::endl;
		continue;
	    }	
	    std::wstring w(word.Ptr, word.Len);
	    wordsToRemove.push_back(w);
        }
    }
    for(auto&& w: wordsToRemove) {
	WordToId.erase(w);
    }
    std::cerr << "[info] cleaned " << wordsToRemove.size() << " words from vocab" << std::endl;
    std::cerr << "[info] vocab size " << WordToId.size() << " after cleaning" << std::endl;
}

bool TLangModel::FinetuneVocab(const std::string vocabFileName, const std::string& alphabetFile) {
    std::cerr << "[info] loading text" << std::endl;
    if (!Tokenizer.LoadAlphabet(alphabetFile)) {
        std::cerr << "[error] failed to load alphabet" << std::endl;
        return false;
    }

    std::wstring vocabText = UTF8ToWide(LoadFile(vocabFileName));
    ToLower(vocabText);
    TSentences sentences = Tokenizer.Process(vocabText);

    if (sentences.empty()) {
        std::cerr << "[error] empty vocab file input" << std::endl;
        return false;
    }
    std::unordered_map<std::wstring, TCount> vocab;
    for (size_t i = 0; i < sentences.size(); ++i) {
        const TWords& words = sentences[i];

        for (auto word: words) {
	    std::wstring w(word.Ptr, word.Len);
            vocab[w] += 1;
        }
    }
    std::vector<std::wstring> wordsToRemove;
    for (auto&& it: WordToId) {
	std::wstring w = it.first;
	if (vocab.find(w) == vocab.end()) {
	    wordsToRemove.push_back(w);   
	}
    }


    std::cerr << "[info] loaded vocab from text, size = " << vocab.size() << std::endl;
    std::cerr << "[info] current model vocab size =" << VocabSize << std::endl;
    std::cerr << "[info] current model has " << wordsToRemove.size() << " words to be removed in this finetuning" << std::endl;

    for(auto&& w: wordsToRemove) {
	WordToId.erase(w);
    }
    VocabSize = WordToId.size();

    std::cerr << "[info] model vocab size after finetune  = " << VocabSize << std::endl;
    return true;
}

bool TLangModel::Train(const std::string& fileName, const std::string& alphabetFile, const int& minWordFreq) {

    std::cerr << "[info] loading text" << std::endl;
    uint64_t trainStarTime = GetCurrentTimeMs();
    if (!Tokenizer.LoadAlphabet(alphabetFile)) {
        std::cerr << "[error] failed to load alphabet" << std::endl;
        return false;
    }
    std::wstring trainText = UTF8ToWide(LoadFile(fileName));
    ToLower(trainText);
    TSentences sentences = Tokenizer.Process(trainText);
    if (sentences.empty()) {
        std::cerr << "[error] no sentences" << std::endl;
        return false;
    }

    TIdSentences sentenceIds = ConvertToIds(sentences);

    assert(sentences.size() == sentenceIds.size());
    {
        std::wstring tmp;
        trainText.swap(tmp);
    }
    {
        TSentences tmp;
        sentences.swap(tmp);
    }

    std::unordered_map<TGram1Key, TCount> grams1;
    std::unordered_map<TGram2Key, TCount, TGram2KeyHash> grams2;
    std::unordered_map<TGram3Key, TCount, TGram3KeyHash> grams3;

    std::cerr << "[info] generating N-grams " << sentenceIds.size() << std::endl;
    uint64_t lastTime = GetCurrentTimeMs();
    size_t total = sentenceIds.size();
    for (size_t i = 0; i < total; ++i) {
        const TWordIds& words = sentenceIds[i];

        for (auto w: words) {
            grams1[w] += 1;
            TotalWords += 1;
        }

        for (ssize_t j = 0; j < (ssize_t)words.size() - 1; ++j) {
            TGram2Key key(words[j], words[j+1]);
            grams2[key] += 1;
        }
        for (ssize_t j = 0; j < (ssize_t)words.size() - 2; ++j) {
            TGram3Key key(words[j], words[j+1], words[j+2]);
            grams3[key] += 1;
        }
        uint64_t currTime = GetCurrentTimeMs();
        if (currTime - lastTime > 4000) {
            std::cerr << "[info] processed " << (100.0 * float(i) / float(total)) << "%" << std::endl;
            lastTime = currTime;
        }
    }

    // remove lower frequency words and ngrams
    if (minWordFreq > 1) {
	RemoveLowFreqWord(grams1, minWordFreq);

	int count;
        count = RemoveLowFreqNgramKeys(grams1, minWordFreq);
	std::cerr << "[info] " << count << " keys are removed from grams1 due to frequency less than " << minWordFreq << std::endl;
        count = RemoveLowFreqNgramKeys(grams2, minWordFreq);
	std::cerr << "[info] " << count << " keys are removed from grams2 due to frequency less than " << minWordFreq << std::endl;
        count = RemoveLowFreqNgramKeys(grams3, minWordFreq);
	std::cerr << "[info] " << count << " keys are removed from grams3 due to frequency less than " << minWordFreq << std::endl;
    }

    VocabSize = grams1.size();

    std::cerr << "[info] generating keys" << std::endl;

    {
        std::vector<std::string> keys;
        keys.reserve(grams1.size() + grams2.size() + grams3.size());

        std::cerr << "[info] ngrams1: " << grams1.size() << "\n";
        std::cerr << "[info] ngrams2: " << grams2.size() << "\n";
        std::cerr << "[info] ngrams3: " << grams3.size() << "\n";
        std::cerr << "[info] total: " << grams3.size() + grams2.size() + grams1.size() << "\n";

        PrepareNgramKeys(grams1, keys);
        PrepareNgramKeys(grams2, keys);
        PrepareNgramKeys(grams3, keys);

        std::cerr << "[info] generating perf hash" << std::endl;

        PerfectHash.Init(keys);
    }

    std::cerr << "[info] finished, buckets: " << PerfectHash.BucketsNumber() << "\n";

    Buckets.resize(PerfectHash.BucketsNumber());
    InitializeBuckets(grams1, PerfectHash, Buckets);
    InitializeBuckets(grams2, PerfectHash, Buckets);
    InitializeBuckets(grams3, PerfectHash, Buckets);

    std::cerr << "[info] buckets filled" << std::endl;

    std::stringbuf checkSumBuf;
    std::ostream checkSumOut(&checkSumBuf);
    NHandyPack::Dump(checkSumOut, trainStarTime, grams1.size(), grams2.size(),
                    grams3.size(), Buckets.size(), trainText.size(), sentences.size());
    std::string checkSumStr = checkSumBuf.str();
    CheckSum = CityHash64(&checkSumStr[0], checkSumStr.size());
    return true;
}

double TLangModel::Score(const TWords& words) const {
    TWordIds sentence;
    for (auto&& w: words) {
        sentence.push_back(GetWordIdNoCreate(w));
    }
    if (sentence.empty()) {
        return std::numeric_limits<double>::min();
    }

    sentence.push_back(UnknownWordId);
    sentence.push_back(UnknownWordId);

    double result = 0;
    for (size_t i = 0; i < sentence.size() - 2; ++i) {
        result += log(GetGram1Prob(sentence[i]));
        result += log(GetGram2Prob(sentence[i], sentence[i + 1]));
        result += log(GetGram3Prob(sentence[i], sentence[i + 1], sentence[i + 2]));
    }
    return result;
}

double TLangModel::Score(const std::wstring& str) const {
    TSentences sentences = Tokenizer.Process(str);
    TWords words;
    for (auto&& s: sentences) {
        for (auto&& w: s) {
            words.push_back(w);
        }
    }
    return Score(words);
}

bool TLangModel::Dump(const std::string& modelFileName) const {
    std::ofstream out(modelFileName, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    NHandyPack::Dump(out, LANG_MODEL_MAGIC_BYTE);
    NHandyPack::Dump(out, LANG_MODEL_VERSION);
    Dump(out);
    NHandyPack::Dump(out, LANG_MODEL_MAGIC_BYTE);
    return true;
}

bool TLangModel::DumpVocab(const std::string& modelVocabFileName, const std::string& modelVocabFreqFileName) const {
    std::wofstream out(modelVocabFileName);
    std::ofstream out_freq(modelVocabFreqFileName);
    if (!out.is_open() || !out_freq.is_open()) {
        return false;
    }

    for (auto&& it: WordToId) {
	out << it.first << L",";
	TCount cnt = GetWordCount(GetWordIdNoCreate(it.first));
	out_freq << cnt << ",";
    }

    return true;
}

bool TLangModel::Load(const std::string& modelFileName) {
    std::ifstream in(modelFileName, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    uint16_t version = 0;
    uint64_t magicByte = 0;
    NHandyPack::Load(in, magicByte);
    if (magicByte != LANG_MODEL_MAGIC_BYTE) {
        return false;
    }
    NHandyPack::Load(in, version);
    if (version != LANG_MODEL_VERSION) {
        return false;
    }
    Load(in);
    magicByte = 0;
    NHandyPack::Load(in, magicByte);
    if (magicByte != LANG_MODEL_MAGIC_BYTE) {
        Clear();
        return false;
    }
    IdToWord.clear();
    IdToWord.resize(LastWordID + 1, L"");
    for (auto&& it: WordToId) {
        IdToWord[it.second] = it.first;
    }
    return true;
}

void TLangModel::Clear() {
    K = LANG_MODEL_DEFAULT_K;
    WordToId.clear();
    LastWordID = 0;
    TotalWords = 0;
    Tokenizer.Clear();
}

const TRobinHash& TLangModel::GetWordToId() {
    return WordToId;
}

TIdSentences TLangModel::ConvertToIds(const TSentences& sentences) {
    TIdSentences newSentences;
    for (size_t i = 0; i < sentences.size(); ++i) {
        const TWords& words = sentences[i];
        TWordIds wordIds;
        for (size_t j = 0; j < words.size(); ++j) {
            const TWord& word = words[j];
            wordIds.push_back(GetWordId(word));
        }
        newSentences.push_back(wordIds);
    }
    return newSentences;
}

TWordId TLangModel::GetWordId(const TWord& word) {
    assert(word.Ptr && word.Len);
    assert(word.Len < 10000);
    std::wstring w(word.Ptr, word.Len);
    auto it = WordToId.find(w);
    if (it != WordToId.end()) {
        return it->second;
    }
    TWordId wordId = LastWordID;
    ++LastWordID;
    it = WordToId.insert(std::make_pair(w, wordId)).first;
    IdToWord.push_back(it->first);
    return wordId;
}

TWordId TLangModel::GetWordIdNoCreate(const TWord& word) const {
    std::wstring w(word.Ptr, word.Len);
    auto it = WordToId.find(w);
    if (it != WordToId.end()) {
        return it->second;
    }
    return UnknownWordId;
}

TWord TLangModel::GetWordById(TWordId wid) const {
    if (wid >= IdToWord.size()) {
        return TWord();
    }
    return TWord(IdToWord[wid]);
}

TCount TLangModel::GetWordCount(TWordId wid) const {
    return GetGram1HashCount(wid);
}

uint64_t TLangModel::GetCheckSum() const {
    return CheckSum;
}

TWord TLangModel::GetWord(const std::wstring& word) const {
    auto it = WordToId.find(word);
    if (it != WordToId.end()) {
        return TWord(&it->first[0], it->first.size());
    }
    return TWord();
}

const std::unordered_set<wchar_t>& TLangModel::GetAlphabet() const {
    return Tokenizer.GetAlphabet();
}

TSentences TLangModel::Tokenize(const std::wstring& text) const {
    return Tokenizer.Process(text);
}

double TLangModel::GetGram1Prob(TWordId word) const {
    double countsGram1 = GetGram1HashCount(word);
    countsGram1 += K;
    return countsGram1 / (TotalWords + VocabSize);
}

double TLangModel::GetGram2Prob(TWordId word1, TWordId word2) const {
    double countsGram1 = GetGram1HashCount(word1);
    double countsGram2 = GetGram2HashCount(word1, word2);
    if (countsGram2 > countsGram1) { // (hash collision)
        countsGram2 = 0;
    }
    countsGram1 += TotalWords;
    countsGram2 += K;
    return countsGram2 / countsGram1;
}

double TLangModel::GetGram3Prob(TWordId word1, TWordId word2, TWordId word3) const {
    double countsGram2 = GetGram2HashCount(word1, word2);
    double countsGram3 = GetGram3HashCount(word1, word2, word3);
    if (countsGram3 > countsGram2) { // hash collision
        countsGram3 = 0;
    }
    countsGram2 += TotalWords;
    countsGram3 += K;
    return countsGram3 / countsGram2;
}

template<typename T>
TCount GetGramHashCount(T key,
                        const TPerfectHash& ph,
                        const std::vector<std::pair<uint16_t, uint16_t>>& buckets)
{
    constexpr int TMP_BUF_SIZE = 128;
    static char tmpBuff[TMP_BUF_SIZE];
    static MemStream tmpBuffStream(tmpBuff, TMP_BUF_SIZE - 1);
    static std::ostream out(&tmpBuffStream);

    tmpBuffStream.Reset();

    NHandyPack::Dump(out, key);

    uint32_t bucket = ph.Hash(tmpBuff, tmpBuffStream.Size());

    assert(bucket < ph.BucketsNumber());
    const std::pair<uint16_t, uint16_t>& data = buckets[bucket];

    TCount res = TCount();
    if (data.first == CityHash16(tmpBuff, tmpBuffStream.Size())) {
        res = UnpackInt32(data.second);
    }
    return res;
}

TCount TLangModel::GetGram1HashCount(TWordId word) const {
    if (word == UnknownWordId) {
        return TCount();
    }
    TGram1Key key = word;
    return GetGramHashCount(key, PerfectHash, Buckets);
}

TCount TLangModel::GetGram2HashCount(TWordId word1, TWordId word2) const {
    if (word1 == UnknownWordId || word2 == UnknownWordId) {
        return TCount();
    }
    TGram2Key key({word1, word2});
    return GetGramHashCount(key, PerfectHash, Buckets);
}

TCount TLangModel::GetGram3HashCount(TWordId word1, TWordId word2, TWordId word3) const {
    if (word1 == UnknownWordId || word2 == UnknownWordId || word3 == UnknownWordId) {
        return TCount();
    }
    TGram3Key key(word1, word2, word3);
    return GetGramHashCount(key, PerfectHash, Buckets);
}

} // NJamSpell
