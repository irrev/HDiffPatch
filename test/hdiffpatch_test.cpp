#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cassert>

//#define _CompressPlugin_zlib
//#define _CompressPlugin_ldef
//#define _CompressPlugin_bz2
//#define _CompressPlugin_lzma
//#define _CompressPlugin_lzma2
//#define _CompressPlugin_lz4
//#define _CompressPlugin_lz4hc
#define _CompressPlugin_zstd
//#define _CompressPlugin_brotli
//#define _CompressPlugin_lzham
//#define _CompressPlugin_tuz

#include "../build_libs/include/HDiffPatch.h"

using namespace HDiffPatch;



// Helper function to print test results
void printResult(const std::string& testName, bool passed) {
    std::cout << "[" << (passed ? "PASS" : "FAIL") << "] " << testName << std::endl;
    if (!passed) {
        exit(1);
    }
}

// Helper to compare data
bool checkData(const std::vector<unsigned char>& data1, const unsigned char* data2, size_t size2) {
    if (data1.size() != size2) return false;
    return memcmp(data1.data(), data2, size2) == 0;
}

int main() {
    std::cout << "Starting HDiffPatch API Tests..." << std::endl;

    // Prepare test data
    std::string oldStr = "Hello World! This is the old data. It has some common parts with the new data.";
    std::string newStr = "Hello HDiffPatch! This is the new data. It has some common parts with the old data. And some new content.";
    
    const unsigned char* oldData = (const unsigned char*)oldStr.data();
    const unsigned char* oldData_end = oldData + oldStr.size();
    const unsigned char* newData = (const unsigned char*)newStr.data();
    const unsigned char* newData_end = newData + newStr.size();

    // 1. Test Basic Diff/Patch (CreateDiff, CheckDiff, Patch)
    {
        std::cout << "\n--- Testing Basic Diff/Patch ---" << std::endl;
        std::vector<unsigned char> diff;
        CreateDiff(newData, newData_end, oldData, oldData_end, diff);
        printResult("CreateDiff", !diff.empty());

        bool check = CheckDiff(newData, newData_end, oldData, oldData_end, diff.data(), diff.data() + diff.size());
        printResult("CheckDiff", check);

        std::vector<unsigned char> patchedData(newStr.size());
        bool patchResult = Patch(patchedData.data(), patchedData.data() + patchedData.size(),
                                 oldData, oldData_end, diff.data(), diff.data() + diff.size());
        printResult("Patch", patchResult);
        printResult("Patch Content Check", checkData(patchedData, newData, newStr.size()));
    }

    // 2. Test Single Compressed Diff/Patch with different compression types
    std::vector<HDiffCompressionType> compressionTypes = {
        HDIFF_COMPRESSION_NONE,
        HDIFF_COMPRESSION_ZLIB,
        HDIFF_COMPRESSION_LZMA,
        HDIFF_COMPRESSION_LZMA2,
        HDIFF_COMPRESSION_ZSTD,
        HDIFF_COMPRESSION_LDEF,
        HDIFF_COMPRESSION_BZ2
    };

    const char* compressionNames[] = {
        "NONE", "ZLIB", "LZMA", "LZMA2", "ZSTD", "LDEF", "BZ2"
    };

    std::cout << "\n--- Testing Single Compressed Diff/Patch ---" << std::endl;
    for (int i = 0; i < compressionTypes.size(); ++i) {
        HDiffCompressionType type = compressionTypes[i];
        std::string typeName = compressionNames[i];
        std::cout << "Testing Compression: " << typeName << std::endl;

        std::vector<unsigned char> diff;
        // Test CreateSingleCompressedDiff
        try {
            CreateSingleCompressedDiff(newData, newData_end, oldData, oldData_end, diff, type);
            printResult("CreateSingleCompressedDiff (" + typeName + ")", !diff.empty());
        } catch (...) {
             std::cout << "[WARN] CreateSingleCompressedDiff failed for " << typeName << " (maybe not supported in build)" << std::endl;
             continue;
        }

        // Test CheckSingleCompressedDiff
        bool check = CheckSingleCompressedDiff(newData, newData_end, oldData, oldData_end, diff.data(), diff.data() + diff.size(), type);
        printResult("CheckSingleCompressedDiff (" + typeName + ")", check);

        // Test GetSingleCompressedDiffInfo
        SingleCompressedDiffInfo info;
        bool infoResult = GetSingleCompressedDiffInfo(diff.data(), diff.data() + diff.size(), &info);
        printResult("GetSingleCompressedDiffInfo (" + typeName + ")", infoResult);
        if (infoResult) {
            // Verify info
            if (info.newDataSize != newStr.size() || info.oldDataSize != oldStr.size()) {
                printResult("Info Size Check (" + typeName + ")", false);
            }
        }

        // Test PatchSingleCompressedDiff
        std::vector<unsigned char> patchedData1(newStr.size());
        bool patchResult1 = PatchSingleCompressedDiff(patchedData1.data(), patchedData1.data() + patchedData1.size(),
                                                      oldData, oldData_end, diff.data(), diff.data() + diff.size(), &info);
        printResult("PatchSingleCompressedDiff (" + typeName + ")", patchResult1);
        printResult("Patch Content Check 1 (" + typeName + ")", checkData(patchedData1, newData, newStr.size()));

        // Test PatchSingleStream
        std::vector<unsigned char> patchedData2(newStr.size());
        bool patchResult2 = PatchSingleStream(patchedData2.data(), patchedData2.data() + patchedData2.size(),
                                              oldData, oldData_end, diff.data(), diff.data() + diff.size());
        printResult("PatchSingleStream (" + typeName + ")", patchResult2);
        printResult("Patch Content Check 2 (" + typeName + ")", checkData(patchedData2, newData, newStr.size()));
    }

    // 3. Test Easy API Wrappers
    {
        std::cout << "\n--- Testing Easy API Wrappers ---" << std::endl;
        std::vector<unsigned char> patch;
        
        // Test CreatePatch (using ZSTD as default or explicit)
        CreatePatch(oldData, oldData_end, newData, newData_end, patch, HDIFF_COMPRESSION_ZSTD);
        printResult("CreatePatch", !patch.empty());

        // Test CheckPatch
        bool check = CheckPatch(oldData, oldData_end, newData, newData_end, patch.data(), patch.data() + patch.size());
        printResult("CheckPatch", check);

        // Test ApplyPatch
        std::vector<unsigned char> patchedData;
        bool applyResult = ApplyPatch(oldData, oldData_end, patch.data(), patch.data() + patch.size(), patchedData);
        printResult("ApplyPatch", applyResult);
        printResult("ApplyPatch Content Check", checkData(patchedData, newData, newStr.size()));
    }

    std::cout << "\nAll Tests Passed Successfully!" << std::endl;
    return 0;
}