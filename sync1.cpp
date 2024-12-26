#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

// 常量
const size_t TS_PACKET_SIZE = 188; // TS 包大小（单位：字节）
const uint8_t SYNC_BYTE = 0x47; // TS 包的同步字节
const size_t WINDOW_SIZE = 5; // 滑动窗口大小，可以根据需要调整

//读取 TS 文件并返回其字节数据
std::vector<uint8_t> readTsFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(1);
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    return data;
}

// 写入对齐后的 TS 数据到新文件
void writeAlignedTsFile(const std::string& filename, const std::vector<uint8_t>& alignedData) {
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        std::cerr << "Failed to write to file: " << filename << std::endl;
        exit(1);
    }
    outFile.write(reinterpret_cast<const char*>(alignedData.data()), alignedData.size());
}
// 滑动窗口判断该字节后是否丢比特
bool isPacketLost(const std::vector<uint8_t>& originalData, const std::vector<uint8_t>& corruptedData, size_t originalPos, size_t corruptedPos) {
    size_t matchCount = 0;
    for (size_t i = 0; i < WINDOW_SIZE && originalPos + i < originalData.size() && corruptedPos + i < corruptedData.size(); ++i) {
        if (originalData[originalPos + i] == corruptedData[corruptedPos + i]) {
            matchCount++;
        }
    }
    return matchCount < WINDOW_SIZE / 2; // 如果匹配的字节数少于窗口大小的一半，认为是丢比特
}
// 从TS包中提取PID
uint16_t Pid(const std::vector<uint8_t>& data, size_t pos) {

    // PID占13位，分别位于第2字节的高5位和第3字节
    uint16_t pid = ((data[pos + 1] & 0x1F) << 8) | data[pos + 2];
    return pid; // 返回PID
}
bool Pid_sync(const std::vector<uint8_t>& data1, const std::vector<uint8_t>& data2, size_t pos1 ,size_t pos2) {
    // 从两个数据中提取PID
    uint16_t pid1 = Pid(data1, pos1);
    uint16_t pid2 = Pid(data2, pos2);

    // 提取后续的两个字节
    uint8_t byte1_1 = data1[pos1 + 3]; // header 最后一个字节
    uint8_t byte1_2 = data1[pos1 + 4]; // payload 第一个字节
    uint8_t byte2_1 = data2[pos2 + 3];
    uint8_t byte2_2 = data2[pos2 + 4];

    // 比较PID和后续字节
    return (pid1 == pid2) && (byte1_1 == byte2_1) && (byte1_2 == byte2_2);

}
struct AlignedDataResult {
    std::vector<uint8_t> alignedData;
    std::vector<uint8_t> diffData;
};
    // 对齐受损 TS 文件和原始 TS 文件
AlignedDataResult alignTsFiles(const std::vector<uint8_t>& originalData, const std::vector<uint8_t>& corruptedData) {
    std::vector<uint8_t> alignedData(originalData.size(), 0); // 预分配大小并初始化为0
	std::vector<uint8_t> diffData(originalData.size(), 0); // 预分配大小并初始化为0
    size_t originalPos = 0;
    size_t corruptedPos = 0;
    // 遍历原始文件的字节
    while (originalPos + TS_PACKET_SIZE <= originalData.size()) {

    // 查找同步字节 (0x47)，确保是有效的 TS 包
    // 优先后向，如果后向无法进入则使用前向
	// 把前向扫描加上，后面控制pb大小了，如果pb过大直接退出
        while (corruptedPos < corruptedData.size()) {
			if (corruptedData[corruptedPos] == SYNC_BYTE ){
                if ((corruptedPos + TS_PACKET_SIZE < corruptedData.size())){
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


        //找到了同步字节，但是PID不一样，说明产生了丢包，需要在originalData里找到对应的包
		if (corruptedPos < corruptedData.size() && (!Pid_sync(originalData, corruptedData, originalPos, corruptedPos))) {
            //std::cout<< "找到了同步字节，但是PID不一样" << std::endl;
			int pb = 0;
			while ( originalPos +  pb * TS_PACKET_SIZE <= originalData.size() ) {
                pb++;

				if (Pid_sync(originalData, corruptedData, originalPos + pb * TS_PACKET_SIZE, corruptedPos)){
					originalPos += pb  * TS_PACKET_SIZE;
                    break;
				}
				if (pb > 12) {  //最多能处理连续两次的丢包
					corruptedPos += 1;
					break;
				}

             }
		}

        // 找到了同步字节，且PID一样，说明这里没有丢包
        if (corruptedPos < corruptedData.size() && Pid_sync(originalData, corruptedData, originalPos, corruptedPos)) {
            // 如果下个包PID仍一样，说明没有丢包
            if (Pid_sync(originalData, corruptedData, originalPos + TS_PACKET_SIZE, corruptedPos + TS_PACKET_SIZE) &&
                originalPos + TS_PACKET_SIZE < originalData.size() &&
                corruptedPos + TS_PACKET_SIZE < corruptedData.size()
                ) {
                std::fill(alignedData.begin() + originalPos, alignedData.begin() + originalPos + TS_PACKET_SIZE, 0xff);
               // std::copy(corruptedData.begin() + corruptedPos, corruptedData.begin() + corruptedPos + TS_PACKET_SIZE, alignedData.begin() + originalPos);
                for (int i = 0; i < TS_PACKET_SIZE; i++) {
                    if (corruptedData[corruptedPos + i] != originalData[originalPos + i]) {
                        diffData[originalPos + i] = corruptedData[corruptedPos + i] ^ originalData[originalPos + i];
                        // diffData[originalPos + i] = !(corruptedData[corruptedPos + i] ^ originalData[originalPos + i]); // 同或：相同为1
                    }
                }
                corruptedPos += TS_PACKET_SIZE;

            }
            // 下个包不同步，说明发生了丢比特，需要逐比特比较
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
        originalPos += TS_PACKET_SIZE;
  

    }
    //打印originalPos和corruptedPos
    std::cout << "originalData size: " << originalPos << std::endl;
    std::cout << "corruptedData size: " << corruptedPos << std::endl;
    return { alignedData, diffData }; // 返回封装的结构体
}

int main() {
    // 读取原始文件和受损文件
    std::vector<uint8_t> originalData = readTsFile("orig.ts");
    std::vector<uint8_t> corruptedData = readTsFile("./Test_ERROR/orig_edit.ts");

    AlignedDataResult result = alignTsFiles(originalData, corruptedData);

    // 写入对齐后的数据到新文件
    writeAlignedTsFile("./Test_ERROR/test_error_aligned.ts", result.alignedData);
    writeAlignedTsFile("./Test_ERROR/test_error.ts", result.diffData);

    // algin和origin按位与
    std::vector<uint8_t> alignedDataYu;
    size_t minSize = std::min(originalData.size(), result.alignedData.size());
    alignedDataYu.reserve(minSize); // 预留空间以提高性能

    for (size_t i = 0; i < minSize; ++i) {
        alignedDataYu.push_back(originalData[i] & result.alignedData[i]);
    }

    // 写入按位与的结果到新文件
    writeAlignedTsFile("./Test_ERROR/test_error_aligned2.ts", alignedDataYu);

    std::cout << "TS files aligned and written to aligned.ts" << std::endl;

    return 0;
}
