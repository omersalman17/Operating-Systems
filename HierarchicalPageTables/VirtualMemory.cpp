#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define PAPA_OFFSET TABLES_DEPTH-1
#define DATA_FRAME_OFFSET TABLES_DEPTH


typedef struct {
    uint64_t frameIndexToEvict;
    uint64_t pageNumberToEvict;
    uint64_t maxWeight;
    uint64_t pAddressOfFrameInPapa;
} EvictionStruct;

void parseVirtualAddress(uint64_t virtualAddress, uint64_t* offsets) {
    uint64_t frameOffsetMask = ((uint64_t)1LL << (uint64_t)OFFSET_WIDTH) - 1;
    for (int i=0; i<TABLES_DEPTH+1; i++) {
        offsets[TABLES_DEPTH-(+i)] = frameOffsetMask & virtualAddress;
        virtualAddress = virtualAddress >> (uint64_t)OFFSET_WIDTH;
    }
}

void clearTable(uint64_t frameIndex) {
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

void VMinitialize() {
    clearTable(0);
}

uint64_t getFrameByDFS(word_t currentFrameIndex, uint64_t* maxFrameUsed, int depthCounter, bool* foundUnusedFrame,
                       uint64_t papaFrameIndex, bool *flag2){

    if (depthCounter == TABLES_DEPTH){
        return currentFrameIndex;
    }
    uint64_t frame;
    bool  flag = true;
    for (int i=0; i<PAGE_SIZE; i++){
        word_t nextFrameIndex;
        PMread((currentFrameIndex) * PAGE_SIZE + i, &nextFrameIndex);
        if (nextFrameIndex != 0){
            flag = false;
            if (nextFrameIndex >(word_t) *maxFrameUsed){
                *maxFrameUsed = nextFrameIndex;
            }
            frame = getFrameByDFS(nextFrameIndex, maxFrameUsed, depthCounter+1, foundUnusedFrame,
                                  papaFrameIndex, flag2);
            if (*foundUnusedFrame){
                if(frame != papaFrameIndex && *flag2){
                    *flag2 = false;
                    PMwrite((currentFrameIndex) * PAGE_SIZE + i, 0);
                }
                return frame;
            }
        }
    }

    // if we reached here it means current frame is full of 0s - unused.
    if (currentFrameIndex != (word_t)papaFrameIndex && flag) {
        *foundUnusedFrame = true;

    }
    return currentFrameIndex;
}


void getFrameForEvictionByDFS(word_t currentFrameIndex, EvictionStruct &evictionStruct, int depthCounter,
                              uint64_t pageNumber, uint64_t currentWeight,  uint64_t pAddressOfFrameInPapa)
{
    currentWeight += ((currentFrameIndex % 2 == 0) ?WEIGHT_EVEN : WEIGHT_ODD);
    if (depthCounter == TABLES_DEPTH){
        currentWeight += ((pageNumber % 2 == 0) ? WEIGHT_EVEN : WEIGHT_ODD);
        if ((currentWeight > evictionStruct.maxWeight) || (currentWeight == evictionStruct.maxWeight &&
                                                        (pageNumber < evictionStruct.pageNumberToEvict))) {
            evictionStruct.maxWeight = currentWeight;
            evictionStruct.frameIndexToEvict = currentFrameIndex;
            evictionStruct.pageNumberToEvict = pageNumber;
            evictionStruct.pAddressOfFrameInPapa = pAddressOfFrameInPapa;
        }
        return;
    }

    for (int i=0; i<PAGE_SIZE; i++){
        word_t nextFrameIndex;
        PMread((currentFrameIndex) * PAGE_SIZE + i, &nextFrameIndex);
        if (nextFrameIndex != 0) {
            uint64_t pageIndex = (pageNumber << OFFSET_WIDTH) +i;
            getFrameForEvictionByDFS(nextFrameIndex, evictionStruct, depthCounter+1, pageIndex,
                                     currentWeight, (currentFrameIndex) * PAGE_SIZE + i);
        }

    }
}

// done using dfs
uint64_t findSuitedFrame(uint64_t papaFrameIndex){
    bool foundUnusedFrame = false;
    uint64_t maxFrameUsed = 0;
    bool flag2 = true;
    uint64_t suitedFrameIndex = getFrameByDFS(0, &maxFrameUsed, 0, &foundUnusedFrame,
                                              papaFrameIndex, &flag2);
    // if case 1:
    if (foundUnusedFrame) {
        if (suitedFrameIndex != papaFrameIndex) {

        }
        return suitedFrameIndex;
    }
    // if case 2
    if (maxFrameUsed + 1 < NUM_FRAMES){
        return maxFrameUsed + 1;
    }
    EvictionStruct evictionStruct = {0, 0, 0, 0};
    getFrameForEvictionByDFS(0, evictionStruct, 0,0,  0, 0);

    PMevict(evictionStruct.frameIndexToEvict, evictionStruct.pageNumberToEvict);
    PMwrite(evictionStruct.pAddressOfFrameInPapa, 0);

    return evictionStruct.frameIndexToEvict;

}

uint64_t getSuitedPAddress(uint64_t virtualAddress){
    uint64_t framesOffsets[TABLES_DEPTH+1];
    parseVirtualAddress(virtualAddress, framesOffsets);
    word_t nextFrameIndex = 0;
    word_t papaFrameIndex = 0;
    // traversing through frames containing next addresses frames
    for (int i=0; i < TABLES_DEPTH - 1; i++) {
        papaFrameIndex = nextFrameIndex;
        PMread(nextFrameIndex * PAGE_SIZE + framesOffsets[i], &nextFrameIndex);
        if (nextFrameIndex == 0) {
            // start searching for unused frame using dfs from the root.
            uint64_t availableFrameIndex = findSuitedFrame(papaFrameIndex);
            clearTable(availableFrameIndex);
            PMwrite(papaFrameIndex * PAGE_SIZE + framesOffsets[i], availableFrameIndex);
            nextFrameIndex = availableFrameIndex;
        }
    }
    papaFrameIndex = nextFrameIndex;
    if(PAPA_OFFSET >= 0){
        PMread(nextFrameIndex * PAGE_SIZE + framesOffsets[PAPA_OFFSET], &nextFrameIndex);
        // we've reached the data frame (addr should point to it)
        if (nextFrameIndex == 0) {
            uint64_t availableFrameIndex = findSuitedFrame(papaFrameIndex);
            // restoring suited page for available frame found
            uint64_t pageIndex = virtualAddress >> OFFSET_WIDTH;
            PMrestore(availableFrameIndex, pageIndex);
            PMwrite(papaFrameIndex * PAGE_SIZE + framesOffsets[PAPA_OFFSET], availableFrameIndex);
            nextFrameIndex = availableFrameIndex;
        }
    }

    return nextFrameIndex * PAGE_SIZE + framesOffsets[DATA_FRAME_OFFSET];
}

int VMread(uint64_t virtualAddress, word_t* value) {
    if(virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return 0;
    }
    PMread(getSuitedPAddress(virtualAddress), value);
    return 1;
}

int VMwrite(uint64_t virtualAddress, word_t value) {
    if(virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return 0;
    }
    PMwrite(getSuitedPAddress(virtualAddress), value);
    return 1;
}
