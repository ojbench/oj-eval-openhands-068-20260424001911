


#include "allocator.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>

struct AllocationInfo {
    void* ptr;
    std::size_t size;
    int id;
};

int main() {
    std::size_t poolSize;
    std::cin >> poolSize;
    
    TLSFAllocator allocator(poolSize);
    
    std::vector<AllocationInfo> allocations;
    std::string command;
    
    while (std::cin >> command) {
        if (command == "ALLOC") {
            std::size_t size;
            int id;
            std::cin >> size >> id;
            
            void* ptr = allocator.allocate(size);
            if (ptr) {
                allocations.push_back({ptr, size, id});
                std::cout << "ALLOC " << id << " SUCCESS" << std::endl;
            } else {
                std::cout << "ALLOC " << id << " FAILED" << std::endl;
            }
        } else if (command == "FREE") {
            int id;
            std::cin >> id;
            
            bool found = false;
            for (auto it = allocations.begin(); it != allocations.end(); ++it) {
                if (it->id == id) {
                    allocator.deallocate(it->ptr);
                    allocations.erase(it);
                    found = true;
                    std::cout << "FREE " << id << " SUCCESS" << std::endl;
                    break;
                }
            }
            
            if (!found) {
                std::cout << "FREE " << id << " NOT_FOUND" << std::endl;
            }
        } else if (command == "INFO") {
            std::cout << "Pool Size: " << allocator.getMemoryPoolSize() << std::endl;
            std::cout << "Max Available: " << allocator.getMaxAvailableBlockSize() << std::endl;
            std::cout << "Active Allocations: " << allocations.size() << std::endl;
        } else if (command == "EXIT") {
            break;
        }
    }
    
    return 0;
}


