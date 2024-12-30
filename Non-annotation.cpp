#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
const size_t TS_PACKET_SIZE = 188;
const uint8_t SYNC_BYTE = 0x47;
const size_t WINDOW_SIZE = 5;
const size_t PCM_PKT_SIZE = 512;

std::vector<uint8_t> readTsFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(1);
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    std::cout << "读取成功" << std::endl;

    return data;
}
void writeAlignedTsFile(const std::string& filename, const std::vector<uint8_t>& alignedData) {
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        std::cerr << "Failed to write to file: " << filename << std::endl;
        exit(1);
    }
    outFile.write(reinterpret_cast<const char*>(alignedData.data()), alignedData.size());
}

bool isPacketLost(const std::vector<uint8_t>& originalData, const std::vector<uint8_t>& corruptedData, size_t originalPos, size_t corruptedPos) {
    size_t matchCount = 0;
    for (size_t i = 0; i < WINDOW_SIZE && originalPos + i < originalData.size() && corruptedPos + i < corruptedData.size(); ++i) {
        if (originalData[originalPos + i] == corruptedData[corruptedPos + i]) {
            matchCount++;
        }
    }
    return matchCount < WINDOW_SIZE / 2;
}

uint16_t Pid(const std::vector<uint8_t>& data, size_t pos) {
    uint16_t pid = ((data[pos + 1] & 0x1F) << 8) | data[pos + 2];
    return pid;
}

bool Pid_sync(const std::vector<uint8_t>& data1, const std::vector<uint8_t>& data2, size_t pos1, size_t pos2) {
    uint16_t pid1 = Pid(data1, pos1);
    uint16_t pid2 = Pid(data2, pos2);

    uint8_t byte1_1 = data1[pos1 + 3];
    uint8_t byte1_2 = data1[pos1 + 4];
    uint8_t byte2_1 = data2[pos2 + 3];
    uint8_t byte2_2 = data2[pos2 + 4];

    return (pid1 == pid2) && (byte1_1 == byte2_1) && (byte1_2 == byte2_2);
}

struct AlignedDataResult {
    std::vector<uint8_t> alignedData;
    std::vector<uint8_t> diffData;
};

AlignedDataResult alignTsFiles(const std::vector<uint8_t>& originalData, const std::vector<uint8_t>& corruptedData) {
    std::vector<uint8_t> alignedData(originalData.size(), 0);
    std::vector<uint8_t> diffData(originalData.size(), 0);
    size_t originalPos = 0;
    size_t corruptedPos = 0;

    while (originalPos + TS_PACKET_SIZE <= originalData.size()) {
        while (corruptedPos < corruptedData.size()) {
            if (corruptedData[corruptedPos] == SYNC_BYTE) {
                if ((corruptedPos + TS_PACKET_SIZE < corruptedData.size())) {
                    if (corruptedData[corruptedPos + TS_PACKET_SIZE] == SYNC_BYTE || (corruptedPos - TS_PACKET_SIZE < corruptedData.size()
                        && corruptedData[corruptedPos - TS_PACKET_SIZE] == SYNC_BYTE)) {
                        break;
                    }
                }
                else {
                    if (corruptedData[corruptedPos - TS_PACKET_SIZE] == SYNC_BYTE) {
                        break;
                    }
                }
            }
            corruptedPos += 1;
        }

        if (corruptedPos < corruptedData.size() && (!Pid_sync(originalData, corruptedData, originalPos, corruptedPos))) {
            int pb = 0;
            while (originalPos + pb * TS_PACKET_SIZE <= originalData.size()) {
                pb++;

                if (Pid_sync(originalData, corruptedData, originalPos + pb * TS_PACKET_SIZE, corruptedPos)) {
                    originalPos += pb * TS_PACKET_SIZE;
                    break;
                }
                if (pb > 12) {
                    corruptedPos += 1;
                    break;
                }
            }
        }

        if (corruptedPos < corruptedData.size() && Pid_sync(originalData, corruptedData, originalPos, corruptedPos)) {
            if (Pid_sync(originalData, corruptedData, originalPos + TS_PACKET_SIZE, corruptedPos + TS_PACKET_SIZE) &&
                originalPos + TS_PACKET_SIZE < originalData.size() &&
                corruptedPos + TS_PACKET_SIZE < corruptedData.size()
                ) {
                std::fill(alignedData.begin() + originalPos, alignedData.begin() + originalPos + TS_PACKET_SIZE, 0xff);
                for (int i = 0; i < TS_PACKET_SIZE; i++) {
                    if (corruptedData[corruptedPos + i] != originalData[originalPos + i]) {
                        diffData[originalPos + i] = corruptedData[corruptedPos + i] ^ originalData[originalPos + i];
                    }
                }
                corruptedPos += TS_PACKET_SIZE;
            }
            else {
                for (int i = 0; i < TS_PACKET_SIZE; i++) {
                    if (corruptedData[corruptedPos + i] != originalData[originalPos + i] && !isPacketLost(originalData, corruptedData, originalPos + i, corruptedPos + i)) {
                        diffData[originalPos + i] = corruptedData[corruptedPos + i] ^ originalData[originalPos + i];
                    }
                    else if (corruptedData[corruptedPos + i] != originalData[originalPos + i] && isPacketLost(originalData, corruptedData, originalPos + i, corruptedPos + i)) {
                        std::fill(alignedData.begin() + originalPos, alignedData.begin() + originalPos + i, 0xff);
                        break;
                    }

                }
                corruptedPos += 1;
            }
        }
		std::cout << originalPos << std::endl;
		std::cout << corruptedPos << std::endl;
        originalPos += TS_PACKET_SIZE;
    }

    return { alignedData, diffData };
}

std::vector<std::vector <uint8_t>> deinterleave(const std::vector <uint8_t>& interleavedData, int channel) {
	std::vector<std::vector <uint8_t>> data(channel);
    size_t dataSize = interleavedData.size();
	int readcount = 0;
	size_t bytetoread = std::min(dataSize - readcount * PCM_PKT_SIZE, PCM_PKT_SIZE);
    while (bytetoread) {
        int ch = channel;
		while (ch) {
			for (size_t i = 8; i < PCM_PKT_SIZE; i += 2) {
                if ((i - 8) % (channel * 2) == (ch - 1) * 2 ) {
					data[ch-1].push_back(interleavedData[i + readcount * PCM_PKT_SIZE + 1]);
					data[ch-1].push_back(interleavedData[i + readcount * PCM_PKT_SIZE]);
				}
			}
            ch--;
		}
        readcount++;
		bytetoread = std::min(dataSize - readcount * PCM_PKT_SIZE, PCM_PKT_SIZE);
    }
    

     //去除冗余数据
    auto removeRedundancy = [](std::vector<uint8_t>& data) {
        std::vector<uint8_t> result;
        size_t dataSize = data.size();
        for (size_t i = 0; i < dataSize; ) {
            if (data[i] == 0x47) {
                if (i + TS_PACKET_SIZE < dataSize && (data[i + TS_PACKET_SIZE] == 0x47 || data[i + TS_PACKET_SIZE] == 0xDE)) {
                    result.insert(result.end(), data.begin() + i, data.begin() + i + TS_PACKET_SIZE);
                    i += TS_PACKET_SIZE;
                    while (i + 1 < dataSize && data[i] == 0xDE && data[i + 1] == 0xFA) {
                        i += 2;
                    }
                }
                else {
                    i++;
                }
            }
            else {
                i++;
            }
        }
        return result;
        };
    
    std::cout << "解交织完毕" << std::endl;

	for (size_t i = 0; i < channel; i++) {
		data[i] = removeRedundancy(data[i]);

	}
    return { data };
}
//输入参数：flag channel interleavedData originalData1 originalData2 ...
int main(int argc, const char** argv) {
	bool flag = std::stoi(argv[1]);
	int channel = std::stoi(argv[2]);


    if (flag) {
        std::vector<uint8_t> InterleavedData = readTsFile(argv[3]);
        std::vector<std::vector<uint8_t>> deinterleavedData = deinterleave(InterleavedData, channel);
        for (size_t i = 0; i < channel; i++) {
            std::vector<uint8_t> originalData = readTsFile(argv[4 + i]);
            AlignedDataResult result = alignTsFiles(originalData, deinterleavedData[i]);
            std::string alignedFilename = "test_error_aligned_" + std::to_string(i) + ".ts";
            std::string diffFilename = "test_error_" + std::to_string(i) + ".ts";
            writeAlignedTsFile(alignedFilename, result.alignedData);
            writeAlignedTsFile(diffFilename, result.diffData);
        }

    }
    else {
        std::vector<uint8_t> originalData = readTsFile(argv[3]);
        std::vector<uint8_t> corruptedData = readTsFile(argv[4]);
		std::cout << "读取完毕" << std::endl;
        AlignedDataResult result = alignTsFiles(originalData, corruptedData);
        writeAlignedTsFile("test_error_aligned.ts", result.alignedData);
        writeAlignedTsFile("test_error.ts", result.diffData);
    }

    return 0;
}

