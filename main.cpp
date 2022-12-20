//=================================================================================================
// loadcontig - Loads a file into the reserved contiguous buffer
//
// Author: Doug Wolf
//=================================================================================================

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <exception>
#include <fcntl.h>
#include <cstring>
#include "PhysMem.h"

using namespace std;

int         fd;             // File descriptor of input file
PhysMem     contigBuffer;   // Manages the reserved contiguous buffer
const char* filename;       // Filename of the file we'll load into the contig buffer

void execute();
void fillBuffer(size_t fileSize);


//=================================================================================================
// main() - Loads a file into the contiguous buffer
//=================================================================================================
int main(int argc, char** argv)
{
    // Ensure that there is a filename on the command line
    if (argv[1] == nullptr)
    {
        fprintf(stderr, "Missing filename on command line\n");
        exit(1);
    }

    // Fetch the name of the file we're going to load
    filename = argv[1];

    try
    {
        execute();
    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(1);
    }
}
//=================================================================================================



//=================================================================================================
// getFileSize() - Returns the size (in bytes) of the input file
//
// Passed:  descriptor = The file descriptor of the file we want the size of
//       
// Returns: The size of the file in bytes
//=================================================================================================
size_t getFileSize(int descriptor)
{
    // Find out how big the file is
    off64_t result = lseek64(descriptor, 0, SEEK_END);

    // Rewind back to the start of the file
    lseek(descriptor, 0, SEEK_SET);

    // And hand the size of the input-file to the caller
    return result;
}
//=================================================================================================


//=================================================================================================
// execute() - Main-line execution
//=================================================================================================
void execute()
{
    // Open the data file
    fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Can't open %s\n", filename);
        exit(1);
    }

    // Find out how big the input file is
    size_t fileSize = getFileSize(fd);

    // Map the entire contiguous buffer
    printf("Mapping contiguous buffer\n");
    contigBuffer.map();

    // Find out how big that buffer is
    size_t bufferSize = contigBuffer.getSize();

    // If the file won't fit into the buffer, complain
    if (fileSize > bufferSize) 
    {
        fprintf(stderr, "File won't fit into contiguous buffer!\n");
        fprintf(stderr, "  File size = %12lu bytes\n", fileSize);
        fprintf(stderr, "Buffer size = %12lu bytes\n", bufferSize);
        exit(1);
    }

    // Load the input file into the contiguous buffer
    fillBuffer(fileSize);

    // Close the input file, we're done
    close(fd);
}
//=================================================================================================





//=================================================================================================
// fillBuffer() - This stuffs some data into the DMA buffer for the purposes of our demo
//
//                          <<< THIS ROUTINE IS A KLUDGE >>>
//
// Because of yet unresolved issues with very slow-writes to the DMA buffer, we are reading the
// file into a local user-space buffer then copying it into the DMA buffer.    For reasons we don't
// yet understand, the MMU allows us to copy a user-space buffer into the DMA space buffer faster
// than it allows us to write to it directly.
//
//                               <<< THIS IS A HACK >>>
//
// The hack will be fixed when we figure out how to write a device driver that can allocate
// very large contiguous blocks.
//   
//=================================================================================================
void fillBuffer(size_t fileSize)
{
  
    // We will load the file into the buffer in blocks of data this size
    const uint32_t FRAME_SIZE = 0x40000000;

    // This is the number of bytes that have been loaded into the buffer
    uint64_t bytesLoaded = 0;

    // Find the physical address of the reserved contiguous buffer
    uint64_t physAddr = contigBuffer.getPhysAddr();

    // Tell the user what's taking so long...
    printf("Loading %s into RAM at address 0x%lX\n", filename, physAddr);

    // Get a pointer to the start of the contiguous buffer
    uint8_t* ptr = contigBuffer.bptr();

    // Allocate a RAM buffer in userspace
    uint8_t* localBuffer = new uint8_t[FRAME_SIZE];

    // Compute how many bytes of data to load...
    uint64_t bytesRemaining = fileSize;

    // Display the completion percentage
    printf("Percent loaded =   0");
    fflush(stdout);

    // While there is still data to load from the file...
    while (bytesRemaining)
    {
        // We'd like to load the entire remainder of the file
        size_t blockSize = bytesRemaining;

        // We're going to load this file in chunks of no more than 1 GB
        if (blockSize > FRAME_SIZE) blockSize = FRAME_SIZE;

        // Load this chunk of the file into our local user-space buffer
        size_t rc = read(fd, localBuffer, blockSize);
        if (rc != blockSize)
        {
            perror("\nread");
            exit(1);
        }

        // Copy the userspace buffer into the contiguous block of physical RAM
        memcpy(ptr, localBuffer, blockSize);

        // Bump the pointer to where the next chunk will be stored
        ptr += blockSize;

        // And keep track of how many bytes are left to load
        bytesRemaining -= blockSize;

        // Compute and display the completion percentage
        bytesLoaded += blockSize;
        int pct = 100 * bytesLoaded / fileSize;
        printf("\b\b\b%3i", pct);
        fflush(stdout);
    }

    // Finish the "percent complete" display
    printf("\b\b\b100\n");

    // Free up the localBuffer so we don't leak memory
    delete[] localBuffer;
}
//=================================================================================================
