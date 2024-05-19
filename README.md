# SFMM - Memory Allocator in C
## Overview
This repository contains the implementation and tests for a simple fixed-size memory management (SFMM) library. The SFMM library provides efficient memory allocation and deallocation functionalities optimized for fixed-size blocks.

## Usage and Capabilities 
The SFMM library is designed to manage memory in blocks of fixed size. This approach optimizes memory allocation and deallocation by reducing fragmentation and overhead associated with managing memory in varying block sizes. The library interacts directly with the system's heap to allocate a large block of memory initially, which is then subdivided into smaller, fixed-size blocks that are managed internally by the library.
### Heap Interaction 
The SFMM library's heap interaction is primarily handled through the initialization and cleanup processes:
- Initialization: During the initialization phase (sfmm_init), the library requests a large contiguous block of memory from the system's heap. This is typically done using system calls like malloc or sbrk, depending on the system and configuration.
- Runtime Management: During the library's operation, memory requests from the application (via sfmm_alloc) are served using the pre-allocated blocks. This minimizes the need for frequent system calls to adjust the heap size, thereby enhancing performance.
- Cleanup: When the library is no longer needed, sfmm_cleanup is called to return all the allocated memory back to the system's heap, ensuring that there are no allocations left hanging, which would otherwise result in memory leaks.

The primary advantage of using the SFMM library is performance. By managing memory in fixed-size blocks, the library significantly reduces the time needed to allocate and deallocate memory. However, this approach may lead to internal fragmentation, especially if the fixed block size does not closely match the size of the majority of the allocation requests. Additionally, because the initial memory allocation is fixed, it might not be suitable for applications with highly variable memory usage patterns.

### Files
- main.c: Contains the main function that initializes and tests the SFMM library functionalities.
- sfmm.c: The core implementation file of the SFMM library. This file includes all the essential functions for memory management including initialization, allocation, deallocation (free), re-allocation, as well as checks for internal fragmentation etc.
- sfmm_tests.c: Includes unit tests for the SFMM library to ensure functionality works as expected under various scenarios.
## Compilation and Running
- gcc -o sfmm main.c sfmm.c sfmm_tests.c sfutil.o
- ./sfmm

#### Acknowledgements
This project is a part of an academic assignment and is used for educational purposes only.
