#include <iostream>

#include <jamspell/lang_model.hpp>
#include <jamspell/spell_corrector.hpp>

using namespace NJamSpell;

void PrintUsage(const char** argv) {
    std::cerr << "Usage: " << argv[0] << " mode args" << std::endl;
    std::cerr << "    train alphabet.txt dataset.txt resultModel.bin minWordFreq  - train model" << std::endl;
    std::cerr << "    score model.bin - input sentences and get score" << std::endl;
    std::cerr << "    correct model.bin - input sentences and get corrected one" << std::endl;
    std::cerr << "    fix model.bin input.txt output.txt - automatically fix txt file" << std::endl;
    std::cerr << "    dump_vocab model.bin vocab.txt vocab_freq.txt - dump a model's vocab into a txt" << std::endl;
    std::cerr << "    finetune_vocab model.bin alphabet.txt vocab.txt resultModel.bin - finetune vocab of model" << std::endl;
}

int Train(const std::string& alphabetFile,
          const std::string& datasetFile,
          const std::string& resultModelFile,
	  const int& minWordFreq)
{
    TLangModel model;
    model.Train(datasetFile, alphabetFile, minWordFreq);
    model.Dump(resultModelFile);
    return 0;
}

int Score(const std::string& modelFile) {
    TLangModel model;
    std::cerr << "[info] loading model" << std::endl;
    if (!model.Load(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    std::cerr << "[info] loaded" << std::endl;
    std::cerr << ">> ";
    for (std::string line; std::getline(std::cin, line);) {
        std::wstring wtext = UTF8ToWide(line);
        std::cerr << model.Score(wtext) << "\n";
        std::cerr << ">> ";
    }
    return 0;
}

int Fix(const std::string& modelFile,
        const std::string& inputFile,
        const std::string& outFile)
{
    TSpellCorrector corrector;
    std::cerr << "[info] loading model" << std::endl;
    if (!corrector.LoadLangModel(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    std::cerr << "[info] loaded" << std::endl;
    std::wstring text = UTF8ToWide(LoadFile(inputFile));
    uint64_t startTime = GetCurrentTimeMs();
    std::wstring result = corrector.FixFragment(text);
    uint64_t finishTime = GetCurrentTimeMs();
    SaveFile(outFile, WideToUTF8(result));
    std::cerr << "[info] process time: " << finishTime - startTime << "ms" << std::endl;
    return 0;
}

int Correct(const std::string& modelFile) {
    TSpellCorrector corrector;
    std::cerr << "[info] loading model" << std::endl;
    if (!corrector.LoadLangModel(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    std::cerr << "[info] loaded" << std::endl;
    std::cerr << ">> ";
    for (std::string line; std::getline(std::cin, line);) {
        std::wstring wtext = UTF8ToWide(line);
        std::wstring result = corrector.FixFragment(wtext);
        std::cerr << WideToUTF8(result) << "\n";
        std::cerr << ">> ";
    }
    return 0;
}

int DumpModelVocab(const std::string& modelFile, const std::string& modelVocabFile, 
		const std::string& modelVocabFreqFile) {
    TLangModel model;
    std::cerr << "[info] loading model" << std::endl;
    if (!model.Load(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    if (!model.DumpVocab(modelVocabFile, modelVocabFreqFile)) {
        std::cerr << "[error] failed to dump vocab of model" << std::endl;
        return 42;
    }
    return 0;
}

int FinetuneVocab(const std::string& modelFile, const std::string& alphabetFile, 
		const std::string& vocabTextFile, const std::string& resultModelFile) {
    TLangModel model;
    
    std::cerr << "[info] loading model" << std::endl;
    if (!model.Load(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }

    if (!model.FinetuneVocab(vocabTextFile, alphabetFile)) {
        std::cerr << "[error] failed to finetune model" << std::endl;
        return 42;
    }

    if (!model.Dump(resultModelFile)) {
        std::cerr << "[error] failed to save finetuned model" << std::endl;
        return 42;
    }
    return 0;
}

int main(int argc, const char** argv) {
    if (argc < 2) {
        PrintUsage(argv);
        return 42;
    }
    std::string mode = argv[1];
    if (mode == "train") {
        if (argc < 5) {
            PrintUsage(argv);
            return 42;
        }
        std::string alphabetFile = argv[2];
        std::string datasetFile = argv[3];
        std::string resultModelFile = argv[4];
		int minWordFreq = 0;
		if (argc >= 6) {
			minWordFreq = std::stoi(argv[5]);
		}
        return Train(alphabetFile, datasetFile, resultModelFile, minWordFreq);
    } else if (mode == "score") {
        if (argc < 3) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        return Score(modelFile);
    } else if (mode == "correct") {
        if (argc < 3) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        return Correct(modelFile);
    } else if (mode == "fix") {
        if (argc < 5) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        std::string inFile = argv[3];
        std::string outFile = argv[4];
        return Fix(modelFile, inFile, outFile);
    } else if (mode == "dump_vocab") {
        if (argc < 5) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        std::string modelVocabFile = argv[3];
	std::string modelVocabFreqFile = argv[4];
        return DumpModelVocab(modelFile, modelVocabFile, modelVocabFreqFile);
    } else if (mode == "finetune_vocab") {
        if (argc < 6) {
            PrintUsage(argv);
            return 42;
        }
	std::string modelFile = argv[2];
        std::string alphabetFile = argv[3];
        std::string vocabTextFile = argv[4];
        std::string resultModelFile = argv[5];
        return FinetuneVocab(modelFile, alphabetFile, vocabTextFile, resultModelFile);
    } 

    PrintUsage(argv);
    return 42;
}
