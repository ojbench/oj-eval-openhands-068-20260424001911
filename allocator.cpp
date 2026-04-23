

#include "allocator.hpp"
#include <cstring>
#include <algorithm>
#include <cassert>

TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) 
    : memoryPool(nullptr), poolSize(memoryPoolSize) {
    // 初始化索引结构
    index.fliBitmap = 0;
    for (int i = 0; i < FLI_SIZE; ++i) {
        index.sliBitmaps[i] = 0;
        for (int j = 0; j < SLI_SIZE; ++j) {
            index.freeLists[i][j] = nullptr;
        }
    }
    
    initializeMemoryPool(memoryPoolSize);
}

TLSFAllocator::~TLSFAllocator() {
    if (memoryPool) {
        ::operator delete(memoryPool);
    }
}

void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    // 分配内存池
    memoryPool = ::operator new(size);
    
    // 创建初始的空闲块
    FreeBlock* initialBlock = reinterpret_cast<FreeBlock*>(memoryPool);
    initialBlock->data = static_cast<char*>(memoryPool) + sizeof(FreeBlock);
    initialBlock->size = size;
    initialBlock->isFree = true;
    initialBlock->prevPhysBlock = nullptr;
    initialBlock->nextPhysBlock = nullptr;
    initialBlock->prevFree = nullptr;
    initialBlock->nextFree = nullptr;
    
    // 插入到空闲列表
    insertFreeBlock(initialBlock);
}

void* TLSFAllocator::allocate(std::size_t size) {
    if (size == 0) return nullptr;
    
    // 对齐大小，至少为sizeof(FreeBlock)
    std::size_t alignedSize = std::max(size, sizeof(FreeBlock));
    alignedSize = (alignedSize + 7) & ~7; // 8字节对齐
    
    // 查找合适的块
    FreeBlock* block = findSuitableBlock(alignedSize);
    if (!block) {
        return nullptr; // 内存不足
    }
    
    // 从空闲列表中移除
    removeFreeBlock(block);
    block->isFree = false;
    
    // 如果块足够大，进行分割
    if (block->size >= alignedSize + sizeof(FreeBlock) + 8) {
        splitBlock(block, alignedSize);
    }
    
    return getDataFromBlock(block);
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    BlockHeader* block = getBlockHeader(ptr);
    if (!block || block->isFree) {
        return; // 无效指针或已经释放
    }
    
    FreeBlock* freeBlock = static_cast<FreeBlock*>(block);
    freeBlock->isFree = true;
    
    // 合并相邻的空闲块
    mergeAdjacentFreeBlocks(freeBlock);
    
    // 插入到空闲列表
    insertFreeBlock(freeBlock);
}

void* TLSFAllocator::getMemoryPoolStart() const {
    return memoryPool;
}

std::size_t TLSFAllocator::getMemoryPoolSize() const {
    return poolSize;
}

std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    // 查找最大的可用块
    std::size_t maxSize = 0;
    
    for (int fli = 0; fli < FLI_SIZE; ++fli) {
        if (!(index.fliBitmap & (1u << fli))) continue;
        
        for (int sli = 0; sli < SLI_SIZE; ++sli) {
            if (!(index.sliBitmaps[fli] & (1u << sli))) continue;
            
            FreeBlock* block = index.freeLists[fli][sli];
            while (block) {
                if (block->isFree && block->size > maxSize) {
                    maxSize = block->size;
                }
                block = block->nextFree;
            }
        }
    }
    
    return maxSize;
}

void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    if (size < 16) {
        fli = 0;
        sli = 0;
        return;
    }
    
    // 计算FLI: floor(log2(size))
    fli = 31 - __builtin_clz(static_cast<unsigned int>(size));
    
    // 计算SLI
    std::size_t flBaseSize = 1u << fli;
    std::size_t remainder = size - flBaseSize;
    std::size_t flRange = flBaseSize / SLI_SIZE;
    if (flRange == 0) flRange = 1;
    
    sli = static_cast<int>(remainder / flRange);
    if (sli >= SLI_SIZE) sli = SLI_SIZE - 1;
}

TLSFAllocator::FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli;
    mappingFunction(size, fli, sli);
    
    // 首先在当前FLI和SLI中查找
    for (int currentFli = fli; currentFli < FLI_SIZE; ++currentFli) {
        int startSli = (currentFli == fli) ? sli : 0;
        
        for (int currentSli = startSli; currentSli < SLI_SIZE; ++currentSli) {
            if (index.freeLists[currentFli][currentSli]) {
                FreeBlock* block = index.freeLists[currentFli][currentSli];
                while (block) {
                    if (block->size >= size) {
                        return block;
                    }
                    block = block->nextFree;
                }
            }
        }
    }
    
    return nullptr;
}

void TLSFAllocator::insertFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    // 插入到空闲链表头部
    block->prevFree = nullptr;
    block->nextFree = index.freeLists[fli][sli];
    
    if (index.freeLists[fli][sli]) {
        index.freeLists[fli][sli]->prevFree = block;
    }
    
    index.freeLists[fli][sli] = block;
    
    // 更新位图
    setFliBit(fli);
    setSliBit(fli, sli);
}

void TLSFAllocator::removeFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    // 从空闲链表中移除
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        index.freeLists[fli][sli] = block->nextFree;
    }
    
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    
    block->prevFree = nullptr;
    block->nextFree = nullptr;
    
    // 更新位图
    if (!index.freeLists[fli][sli]) {
        clearSliBit(fli, sli);
        if (index.sliBitmaps[fli] == 0) {
            clearFliBit(fli);
        }
    }
}

void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t size) {
    std::size_t remainingSize = block->size - size;
    
    // 创建新的空闲块
    FreeBlock* newBlock = reinterpret_cast<FreeBlock*>(
        reinterpret_cast<char*>(block) + size
    );
    
    newBlock->data = reinterpret_cast<char*>(newBlock) + sizeof(FreeBlock);
    newBlock->size = remainingSize;
    newBlock->isFree = true;
    newBlock->prevPhysBlock = block;
    newBlock->nextPhysBlock = block->nextPhysBlock;
    newBlock->prevFree = nullptr;
    newBlock->nextFree = nullptr;
    
    // 更新原块
    block->size = size;
    block->nextPhysBlock = newBlock;
    
    // 更新下一个块的物理前驱
    if (newBlock->nextPhysBlock) {
        newBlock->nextPhysBlock->prevPhysBlock = newBlock;
    }
    
    // 插入新块到空闲列表
    insertFreeBlock(newBlock);
}

void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    // 合并前一个块
    if (block->prevPhysBlock && block->prevPhysBlock->isFree) {
        FreeBlock* prevBlock = static_cast<FreeBlock*>(block->prevPhysBlock);
        removeFreeBlock(prevBlock);
        
        prevBlock->size += block->size;
        prevBlock->nextPhysBlock = block->nextPhysBlock;
        
        if (block->nextPhysBlock) {
            block->nextPhysBlock->prevPhysBlock = prevBlock;
        }
        
        block = prevBlock;
    }
    
    // 合并后一个块
    if (block->nextPhysBlock && block->nextPhysBlock->isFree) {
        FreeBlock* nextBlock = static_cast<FreeBlock*>(block->nextPhysBlock);
        removeFreeBlock(nextBlock);
        
        block->size += nextBlock->size;
        block->nextPhysBlock = nextBlock->nextPhysBlock;
        
        if (nextBlock->nextPhysBlock) {
            nextBlock->nextPhysBlock->prevPhysBlock = block;
        }
    }
}

int TLSFAllocator::findFirstSetBit(std::uint32_t bitmap) const {
    if (bitmap == 0) return -1;
    return __builtin_ctz(bitmap);
}

int TLSFAllocator::findFirstSetBit(std::uint16_t bitmap) const {
    if (bitmap == 0) return -1;
    return __builtin_ctz(bitmap);
}

void TLSFAllocator::setFliBit(int fli) {
    index.fliBitmap |= (1u << fli);
}

void TLSFAllocator::clearFliBit(int fli) {
    index.fliBitmap &= ~(1u << fli);
}

void TLSFAllocator::setSliBit(int fli, int sli) {
    index.sliBitmaps[fli] |= (1u << sli);
}

void TLSFAllocator::clearSliBit(int fli, int sli) {
    index.sliBitmaps[fli] &= ~(1u << sli);
}

TLSFAllocator::BlockHeader* TLSFAllocator::getBlockHeader(void* ptr) const {
    if (!ptr) return nullptr;
    
    // 检查指针是否在内存池范围内
    if (ptr < memoryPool || ptr >= static_cast<char*>(memoryPool) + poolSize) {
        return nullptr;
    }
    
    // 假设指针指向数据区域，向前查找块头
    char* current = static_cast<char*>(ptr);
    char* poolStart = static_cast<char*>(memoryPool);
    
    // 简单实现：假设块头在数据之前
    BlockHeader* header = reinterpret_cast<BlockHeader*>(current - sizeof(BlockHeader));
    
    // 验证块头的有效性
    if (header >= reinterpret_cast<BlockHeader*>(poolStart) && 
        header < reinterpret_cast<BlockHeader*>(poolStart + poolSize) &&
        header->data == ptr) {
        return header;
    }
    
    return nullptr;
}

TLSFAllocator::FreeBlock* TLSFAllocator::getFreeBlockFromData(void* ptr) const {
    return static_cast<FreeBlock*>(getBlockHeader(ptr));
}

void* TLSFAllocator::getDataFromBlock(BlockHeader* block) const {
    return block ? block->data : nullptr;
}

