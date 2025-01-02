#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <string>
#include <filesystem>

const size_t TS_PACKET_SIZE = 188;
const uint8_t SYNC_BYTE = 0x47;
const size_t WINDOW_SIZE = 5;
const size_t PCM_PKT_SIZE = 512;

std::vector<uint8_t> readTsFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(1);
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    return data;
}

void writeAlignedTsFile(const std::string &filename, const std::vector<uint8_t> &alignedData)
{
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Failed to write to file: " << filename << std::endl;
        exit(1);
    }
    outFile.write(reinterpret_cast<const char *>(alignedData.data()), alignedData.size());
}

bool isPacketLost(const std::vector<uint8_t> &originalData, const std::vector<uint8_t> &corruptedData, size_t originalPos, size_t corruptedPos)
{
    size_t matchCount = 0;
    for (size_t i = 0; i < WINDOW_SIZE && originalPos + i < originalData.size() && corruptedPos + i < corruptedData.size(); ++i)
    {
        if (originalData[originalPos + i] == corruptedData[corruptedPos + i])
        {
            matchCount++;
        }
    }
    return matchCount < WINDOW_SIZE / 2;
}

uint16_t Pid(const std::vector<uint8_t> &data, size_t pos)
{
    uint16_t pid = ((data[pos + 1] & 0x1F) << 8) | data[pos + 2];
    return pid;
}

bool Pid_sync(const std::vector<uint8_t> &data1, const std::vector<uint8_t> &data2, size_t pos1, size_t pos2)
{
    if ((pos1 + 4 < data1.size()) && (pos2 + 4 < data2.size()))
    {

        uint16_t pid1 = Pid(data1, pos1);
        uint16_t pid2 = Pid(data2, pos2);

        uint8_t byte1_1 = data1[pos1 + 3];
        uint8_t byte1_2 = data1[pos1 + 4];
        uint8_t byte2_1 = data2[pos2 + 3];
        uint8_t byte2_2 = data2[pos2 + 4];

        return (pid1 == pid2) && (byte1_1 == byte2_1) && (byte1_2 == byte2_2);
    }
    else
    {
        return false;
    }
}

struct AlignedDataResult
{
    std::vector<uint8_t> alignedData;
    std::vector<uint8_t> diffData;
};

AlignedDataResult alignTsFiles(const std::vector<uint8_t> &originalData, const std::vector<uint8_t> &corruptedData)
{
    std::vector<uint8_t> alignedData(originalData.size(), 0);
    std::vector<uint8_t> diffData(originalData.size(), 0);
    size_t originalPos = 0;
    size_t corruptedPos = 0;

    while (originalPos + TS_PACKET_SIZE <= originalData.size())
    {
        while (corruptedPos < corruptedData.size())
        {
            if (corruptedData[corruptedPos] == SYNC_BYTE)
            {
                if ((corruptedPos + TS_PACKET_SIZE < corruptedData.size()))
                {
                    if (corruptedData[corruptedPos + TS_PACKET_SIZE] == SYNC_BYTE || (corruptedPos - TS_PACKET_SIZE < corruptedData.size() && corruptedData[corruptedPos - TS_PACKET_SIZE] == SYNC_BYTE))
                    {
                        break;
                    }
                }
                else
                {
                    if (corruptedData[corruptedPos - TS_PACKET_SIZE] == SYNC_BYTE)
                    {
                        break;
                    }
                }
            }
            corruptedPos += 1;
        }

        if (corruptedPos < corruptedData.size() && (!Pid_sync(originalData, corruptedData, originalPos, corruptedPos)))
        {
            int pb = 0;
            while (originalPos + pb * TS_PACKET_SIZE <= originalData.size())
            {
                pb++;

                if (Pid_sync(originalData, corruptedData, originalPos + pb * TS_PACKET_SIZE, corruptedPos))
                {
                    originalPos += pb * TS_PACKET_SIZE;
                    break;
                }
                if (pb > 12)
                {
                    corruptedPos += 1;
                    break;
                }
            }
        }

        if (corruptedPos < corruptedData.size() && Pid_sync(originalData, corruptedData, originalPos, corruptedPos))
        {
            if (Pid_sync(originalData, corruptedData, originalPos + TS_PACKET_SIZE, corruptedPos + TS_PACKET_SIZE) &&
                originalPos + TS_PACKET_SIZE < originalData.size() &&
                corruptedPos + TS_PACKET_SIZE < corruptedData.size())
            {
                std::fill(alignedData.begin() + originalPos, alignedData.begin() + originalPos + TS_PACKET_SIZE, 0xff);
                for (int i = 0; i < TS_PACKET_SIZE; i++)
                {
                    if (corruptedData[corruptedPos + i] != originalData[originalPos + i])
                    {
                        diffData[originalPos + i] = corruptedData[corruptedPos + i] ^ originalData[originalPos + i];
                    }
                }
                corruptedPos += TS_PACKET_SIZE;
            }
            else
            {
                for (int i = 0; i < TS_PACKET_SIZE; i++)
                {
                    if (corruptedData[corruptedPos + i] != originalData[originalPos + i] && !isPacketLost(originalData, corruptedData, originalPos + i, corruptedPos + i))
                    {
                        diffData[originalPos + i] = corruptedData[corruptedPos + i] ^ originalData[originalPos + i];
                    }
                    else if (corruptedData[corruptedPos + i] != originalData[originalPos + i] && isPacketLost(originalData, corruptedData, originalPos + i, corruptedPos + i))
                    {
                        std::fill(alignedData.begin() + originalPos, alignedData.begin() + originalPos + i, 0xff);
                        break;
                    }
                }
                corruptedPos += 1;
            }
        }
        originalPos += TS_PACKET_SIZE;
    }

    std::cout << "originalData size: " << originalPos << std::endl;
    std::cout << "corruptedData size: " << corruptedPos << std::endl;
    return {alignedData, diffData};
}

std::vector<std::vector<uint8_t>> deinterleave(const std::vector<uint8_t> &interleavedData, int channel)
{
    size_t dataSize = interleavedData.size();
    size_t estimatedSizePerChannel = dataSize / channel / 2; // 大致估计每个通道的数据量
    std::vector<std::vector<uint8_t>> data(channel, std::vector<uint8_t>());
    for (int i = 0; i < channel; ++i)
    {
        data[i].reserve(estimatedSizePerChannel); // 预先分配空间
    }

    int readcount = 0;
    size_t bytetoread = std::min(dataSize - readcount * PCM_PKT_SIZE, PCM_PKT_SIZE);
    while (bytetoread)
    {
        int ch = channel;
        while (ch)
        {
            for (size_t i = 8; i < PCM_PKT_SIZE; i += 2)
            {
                if ((i - 8) % (channel * 2) == (ch - 1) * 2)
                {
                    data[ch - 1].push_back(interleavedData[i + readcount * PCM_PKT_SIZE + 1]);
                    data[ch - 1].push_back(interleavedData[i + readcount * PCM_PKT_SIZE]);
                }
            }
            ch--;
        }
        readcount++;
        bytetoread = std::min(dataSize - readcount * PCM_PKT_SIZE, PCM_PKT_SIZE);
    }

    // 去除冗余数据
    auto removeRedundancy = [](std::vector<uint8_t> &data)
    {
        std::vector<uint8_t> result;
        size_t dataSize = data.size();
        for (size_t i = 0; i < dataSize;)
        {
            if (data[i] == 0x47)
            {
                if (i + TS_PACKET_SIZE < dataSize && (data[i + TS_PACKET_SIZE] == 0x47 || data[i + TS_PACKET_SIZE] == 0xDE))
                {
                    if (data[i + TS_PACKET_SIZE - 4] == 0xDE && data[i + TS_PACKET_SIZE - 3] == 0xFA &&
                        data[i + TS_PACKET_SIZE - 2] == 0xDE && data[i + TS_PACKET_SIZE - 1] == 0xFA)
                    {        // 当0x47的TS包因丢UDP包导致尾部残缺，而下一个UDP包开头得到的包恰好是填充序列
                        i++; // 此时该包是残缺包，不应该写入channel
                    } // 当TS因丢UDP包导致“头有尾丢”这里直接丢掉了一整个TS；若要保留前部分，需添加逐字节写入代码。
                    else
                    {
                        result.insert(result.end(), data.begin() + i, data.begin() + i + TS_PACKET_SIZE);
                        i += TS_PACKET_SIZE;
                    }
                    while (i + 1 < dataSize && data[i] == 0xDE && data[i + 1] == 0xFA)
                    {
                        i += 2;
                    }
                }
                else
                {
                    i++; // origin 用不到，不会出现这种情况；损失数据把有头0x47，但缺尾的包丢掉（这样也可以，align会检测出来）
                }
            }
            else
            {
                i++; // origin 用不到，首字节一定是0x47
            }
        }
        return result;
    };

    std::cout << "解交织完毕" << std::endl;

    for (size_t i = 0; i < channel; i++)
    {
        data[i] = removeRedundancy(data[i]);
    }
    return {data};
}
// 输入参数：flag channel interleavedData originalData1 originalData2 ...
int main(int argc, const char **argv)
{
    int flag = std::stoi(argv[1]);

    switch (flag)
    {
    case 1:
    {
        // 模式1，错误对比
        if (argc != 4)
        {
            std::cerr << " 模式 1 需要 3 个参数: flag origin.ts error.ts" << std::endl;
            return 1;
        }
        std::vector<uint8_t> originalData = readTsFile(argv[2]);
        std::vector<uint8_t> corruptedData = readTsFile(argv[3]);
        AlignedDataResult result = alignTsFiles(originalData, corruptedData);
        std::cout << "TS files aligned and written to aligned.ts/error.ts" << std::endl;
        // std::string alignedFilename = "aligned.ts";
        // std::string diffFilename = "error.ts";

        // 创建目录
        std::string folderName = "flag1";
        std::filesystem::create_directory(folderName);
        std::string alignedFilename = folderName + "aligned.ts";
        std::string diffFilename = folderName + "error.ts";
        writeAlignedTsFile(alignedFilename, result.alignedData);
        writeAlignedTsFile(diffFilename, result.diffData);
        break;
    }
    case 2:
    {
        // 模式2，交织文件解交织
        if (argc != 4)
        {
            std::cerr << " 模式 2 需要 3 个参数: flag channel file.dat" << std::endl;
            return 1;
        }
        int channel = std::stoi(argv[2]);
        std::vector<uint8_t> interleavedData = readTsFile(argv[3]);
        std::vector<std::vector<uint8_t>> deinterleavedData = deinterleave(interleavedData, channel);

        // 创建目录
        std::string folderName = "flag2_" + std::filesystem::path(argv[3]).stem().string();
        std::filesystem::create_directory(folderName);

        // 将解交织后的数据写入文件
        for (size_t i = 0; i < channel; i++)
        {
            std::string deinterleaveFilename = folderName + "/deinterleaved_" + std::to_string(i) + ".ts";
            writeAlignedTsFile(deinterleaveFilename, deinterleavedData[i]);
        }
        break;
    }

    case 3:
    {
        // 模式3：错误交织的文件与原始文件对比
        if (argc < 5)
        {
            std::cerr << " 至少需要4个参数: flag channel interleavedError.dat originalData1 originalData2,..." << std::endl;
            return 1;
        }
        int channel = std::stoi(argv[2]);
        std::vector<uint8_t> interleavedErrorData = readTsFile(argv[3]);

        // 解交织
        std::vector<std::vector<uint8_t>> deinterleavedData = deinterleave(interleavedErrorData, channel);

        // 创建目录
        std::string folderName = "flag3_" + std::filesystem::path(argv[3]).stem().string();
        std::filesystem::create_directory(folderName);

        // 对比每个通道
        for (size_t i = 0; i < channel; i++)
        {
            std::vector<uint8_t> originalData = readTsFile(argv[4 + i]);
            AlignedDataResult result = alignTsFiles(originalData, deinterleavedData[i]);

            std::string alignedFilename = folderName + "/aligned_" + std::to_string(i) + ".ts";
            std::string diffFilename = folderName + "/diff_" + std::to_string(i) + ".ts";
            writeAlignedTsFile(alignedFilename, result.alignedData);
            writeAlignedTsFile(diffFilename, result.diffData);
        }
        break;
    }

    default:
        std::cerr << "未知的 flag 值！" << std::endl;
        return 1;
    }
    return 0;

    // int channel = std::stoi(argv[2]);

    // if (flag)
    // {
    //     std::vector<uint8_t> InterleavedData = readTsFile(argv[3]);
    //     std::vector<std::vector<uint8_t>> deinterleavedData = deinterleave(InterleavedData, channel);
    //     for (size_t i = 0; i < channel; i++)
    //     {
    //         std::vector<uint8_t> originalData = readTsFile(argv[4 + i]);
    //         AlignedDataResult result = alignTsFiles(originalData, deinterleavedData[i]);
    //         std::string alignedFilename = "test_error_aligned_" + std::to_string(i) + ".ts";
    //         std::string diffFilename = "test_error_" + std::to_string(i) + ".ts";
    //         writeAlignedTsFile(alignedFilename, result.alignedData);
    //         writeAlignedTsFile(diffFilename, result.diffData);
    //     }
    // }
    // else
    // {
    //     std::vector<uint8_t> originalData = readTsFile(argv[2]);
    //     std::vector<uint8_t> corruptedData = readTsFile(argv[3]);
    //     std::cout << "读取完毕" << std::endl;
    //     AlignedDataResult result = alignTsFiles(originalData, corruptedData);
    //     writeAlignedTsFile("test_error_aligned.ts", result.alignedData);
    //     writeAlignedTsFile("test_error.ts", result.diffData);
    // }

    // return 0;
}

// int main()
// {
//     std::vector<uint8_t> originalData = readTsFile("orig.ts");
//     std::vector<uint8_t> corruptedData = readTsFile("./Test_ERROR/orig_edit.ts");

//     AlignedDataResult result = alignTsFiles(originalData, corruptedData);

//     writeAlignedTsFile("./Test_ERROR/test_error_aligned.ts", result.alignedData);
//     writeAlignedTsFile("./Test_ERROR/test_error.ts", result.diffData);

//     std::vector<uint8_t> alignedDataYu;
//     size_t minSize = std::min(originalData.size(), result.alignedData.size());
//     alignedDataYu.reserve(minSize);

//     for (size_t i = 0; i < minSize; ++i)
//     {
//         alignedDataYu.push_back(originalData[i] & result.alignedData[i]);
//     }

//     writeAlignedTsFile("./Test_ERROR/test_error_aligned2.ts", alignedDataYu);

//     std::cout << "TS files aligned and written to aligned.ts" << std::endl;

//     return 0;
// }
