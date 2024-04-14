#include "Hasher.hpp"
#include "text.hpp"
#include <unordered_set>
#include <experimental/filesystem>
#include <vector>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "simdjson.h"
#include <thread>
#include <mutex>

using namespace simdjson;
namespace fs = std::experimental::filesystem;

std::mutex hashMutex;

void processFile(const std::string &filePath, const std::string &outputDir, std::unordered_set<std::string> &processedHashes){
    std::string outputFileName = outputDir + "/" + fs::path(filePath).filename().string();

    // Skip processing if output file already exists
    if (fs::exists(outputFileName)) {
        std::cout << "Skipping already processed file: " << filePath << std::endl;
        return;
    }

    std::cout << "\nProcessing file: " << filePath << std::endl;    
    Hasher hasher(5, 100, 10, 10);
    std::vector<std::string> outputLines;

    ondemand::parser parser;
    padded_string json = padded_string::load(filePath);    
    ondemand::document_stream docs = parser.iterate_many(json);
    size_t duplicatedCount = 0;
    int i = 0;
    for (auto doc : docs) {
        std::string textContent;
        std::string_view res;
        auto error = doc["text"].get(res);
        if (!error) {
            textContent = std::string(res);
            text myText(textContent);
            hasher.apply(myText);

            bool isDuplicate = false;
            {
                std::lock_guard<std::mutex> lock(hashMutex);
                for (const std::string& hashValue : myText.getHashes()) {
                    if (processedHashes.find(hashValue) != processedHashes.end()) {
                        isDuplicate = true;
                        duplicatedCount++;
                        break;
                    }
                }

                if (!isDuplicate) {
                    for (const std::string& hashValue : myText.getHashes()) {
                        processedHashes.insert(hashValue);
                    }
                }
            }

            if (!isDuplicate) {
                outputLines.push_back(textContent);
            }
        }
        if (i % 5000 == 0) {
            std::cout << "    \r" << i << std::flush;
        }
        i++;
    }
    std::cout << "\nDuplicated: " << duplicatedCount << std::endl;

    std::ofstream outFile(outputFileName);
    for (const auto &line : outputLines) {
        nlohmann::json li;
        li["text"] = line;
        outFile << li.dump() << std::endl;
    }
    outFile.close();
}

void processFiles(const std::string &inputDir, const std::string &outputDir){
    std::unordered_set<std::string> processedHashes;
    std::vector<std::thread> threads;
    for (const auto &file : fs::directory_iterator(inputDir)) {
        threads.emplace_back(processFile, file.path().string(), outputDir, std::ref(processedHashes));
    }

    for (auto &t : threads) {
        t.join();
    }
}

int main(int argc, char *argv[]){    
    if (argc < 3){
        std::cerr << "Usage: " << argv[0] << " <input directory> <output directory>" << std::endl;
        return 1;
    }

    std::string inputDir = argv[1];
    std::string outputDir = argv[2];

    processFiles(inputDir, outputDir);
    return 0;
}
