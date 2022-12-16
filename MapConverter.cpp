#include <string_view>
#include <zlib.h>
#include <string>
#include <stack>
#include <vector>
#include <unordered_map>
#include <variant>
#include <ranges>
#include <fstream>
#include <filesystem>
#include <string_view>
#include <iostream>
#include <algorithm>
#include <chrono>

auto ExtractMapInfos(std::string const& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::size_t fileSize = file.tellg();
    std::vector<unsigned char> bytes(fileSize);

    file.seekg(0);
    file.read((char*)bytes.data(), fileSize);

    std::size_t uncompressedSize = *(uint32_t*)(&bytes[4]);
    std::vector<unsigned char> uncompressedData(uncompressedSize);

    auto success = uncompress(uncompressedData.data(), (uLongf*)&uncompressedSize, &bytes[8], fileSize - 8);

    std::vector<std::string_view> constantPool;
    std::unordered_map<std::string, std::variant<bool, uint32_t, std::string>> values;
    std::stack<std::variant<bool, uint32_t, std::string>> stack;

    for (int i = 0; i < uncompressedData.size();)
    {
        // ConstantPool
        // U8	: 0x88
        // U16	: poolSize (total characters + nEntries)
        // U16	: nEntries
        if (uncompressedData[i] == 0x88)
        {
            i++;
            uint16_t constantPoolSize = *(uint16_t*)&uncompressedData[i];
            i += 2;
            uint16_t numEntries = *(uint16_t*)&uncompressedData[i];
            i += 2;
            std::string_view constantPoolData((char*)&uncompressedData[i], constantPoolSize - 2);
            auto entries = (constantPoolData | std::views::split('\0'));
            constantPool.assign(entries.begin(), entries.end());
            i += constantPoolSize - 2;
        }
        // Push
        // U8	: 0x96
        // U16	: nBytes (how many bytes in the instruction)
        // U8	: operand type
        else if (uncompressedData[i] == 0x96)
        {
            i++;
            uint16_t nBytes = *(uint16_t*)&uncompressedData[i];
            i += 2;

            while (nBytes > 0)
            {
                const uint8_t type = *(uint8_t*)&uncompressedData[i];

                i++;
                nBytes--;

                if (type == 5) // bool
                {
                    const bool value = *(bool*)&uncompressedData[i];
                    i++;
                    nBytes--;
                    stack.push(value);
                    //std::cout << "Push " << std::boolalpha << value << std::endl;
                }
                else if (type == 7) // uint32
                {
                    const uint32_t value = *(uint32_t*)&uncompressedData[i];
                    i += 4;
                    nBytes -= 4;
                    stack.push(value);
                    //std::cout << "Push " << value << std::endl;
                }
                else if (type == 8) // pool index8
                {
                    const uint8_t poolIndex = *(uint8_t*)&uncompressedData[i];
                    i++;
                    nBytes--;
                    stack.push(std::string{ constantPool[poolIndex] });
                    //std::cout << "Push " << constantPool[poolIndex] << std::endl;
                }
            }
        }
        else if (uncompressedData[i] == 0x1d) // SetVariable
        {
            if (stack.size() > 1)
            {
                const auto op1 = stack.top();
                stack.pop();
                const auto op2 = stack.top();
                stack.pop();

                if (std::holds_alternative<std::string>(op2))
                {
                    const auto& key = std::get<std::string>(op2);
                    values[key] = op1;
                }
            }
            i++;
        }
        else if (uncompressedData[i] == 0x1c) // GetMember
        {
            i++;
        }
        else if (uncompressedData[i] == 0x52) // CallMethod
        {
            i++;
        }
        else if (uncompressedData[i] == 0x17) // Pop
        {
            i++;
        }
        else
            i++;
    }

    return values;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "USAGE: ./mapconverter <input_folder>" << std::endl;
        return 1;
    }

    const auto inputPath = std::filesystem::path(argv[1]);
    const auto outputFolder = inputPath / "output";

    if (!std::filesystem::exists(outputFolder) || !std::filesystem::is_directory(outputFolder))
    {
        if (!std::filesystem::create_directory(outputFolder))
        {
            std::cout << "Could not create output directory" << std::endl;
            return 1;
        }
    }

    auto nFiles = std::count_if(std::filesystem::directory_iterator(inputPath), std::filesystem::directory_iterator{}, [](std::filesystem::directory_entry const& entry) { 
        return std::filesystem::is_regular_file(entry) && entry.path().has_extension() && !entry.path().extension().compare(".swf");
    });

    std::cout << "--------------------------" << std::endl;
    std::cout << "Found " << std::setw(4) << nFiles << " files to export" << std::endl;
    std::cout << "--------------------------" << std::endl;

    unsigned int errors = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (const auto& path : std::filesystem::directory_iterator(inputPath))
    {
        if (!path.is_regular_file())
            continue;

        const auto extension = path.path().extension();
        const auto mapInfos = ExtractMapInfos(path.path().string());
        const auto outFile = outputFolder / path.path().filename().replace_extension(".json");

        if (std::ofstream jsonFile(outFile.string(), std::ios::out); jsonFile)
        {
            try
            {
                auto json = std::format(R"({{"id":{},"width":{},"height":{},"bOutdoor":{},"capabilities":{},"backgroundNum":{},"ambianceId":{},"musicId":{},"mapData":"{}","canAggro":{},"canUseObject":{},"canChangeCharac":{}}})",
                    std::get<uint32_t>(mapInfos.at("id")),
                    std::get<uint32_t>(mapInfos.at("width")),
                    std::get<uint32_t>(mapInfos.at("height")),
                    std::get<bool>(mapInfos.at("bOutdoor")),
                    std::get<uint32_t>(mapInfos.at("capabilities")),
                    std::get<uint32_t>(mapInfos.at("backgroundNum")),
                    std::get<uint32_t>(mapInfos.at("ambianceId")),
                    std::get<uint32_t>(mapInfos.at("musicId")),
                    std::get<std::string>(mapInfos.at("mapData")).data(),
                    std::get<bool>(mapInfos.at("canAggro")),
                    std::get<bool>(mapInfos.at("canUseObject")),
                    std::get<bool>(mapInfos.at("canChangeCharac")));

                jsonFile << json;

                jsonFile.close();
            }
            catch (std::exception& ex)
            {
                errors++;
                std::cout << "[ERROR] " << ex.what() << std::endl;
            }
        }
        else
        {
            errors++;
            std::cout << "Could not open file '" << outFile.string() << "' for writing" << std::endl;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = (endTime - startTime);

    std::cout << "Task completed in " << std::format("{0:%T}", duration) << " with " << errors << " errors." << std::endl;

    return 0;
}
