// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
  i32 fbn = bfsTell(fd) / BYTESPERBLOCK;
  i32 inum = bfsFdToInum(fd);
  int tempNumb = numb;
  if (tempNumb > BYTESPERBLOCK) {
    // fetch the block and allocate buffer
    i8 wholeBuffer[tempNumb];
    int bytesRead = 0;
    int remainingBytes = tempNumb;
    while (remainingBytes > 0) {
      // storing a single block in a allocated buffer
      i8 blockBuffer[BYTESPERBLOCK];
      // reading block from disc
      bfsRead(inum, fbn, blockBuffer);
      int bytesToRead;
      // determine bytes to read from the block
      if (remainingBytes > BYTESPERBLOCK) {
        bytesToRead = BYTESPERBLOCK;
      } 
      else {
        bytesToRead = remainingBytes;
      }
      memcpy(&wholeBuffer[bytesRead], blockBuffer, bytesToRead);
      bytesRead += bytesToRead;
      remainingBytes -= bytesToRead;
      fbn++;
    }
    memcpy(buf, wholeBuffer, tempNumb);
    // counting left over zeros in the last block
    int notRead = 0;
    fbn--;
    i8 blockBuffer[BYTESPERBLOCK];
    bfsRead(inum, fbn, blockBuffer);
    for (int i = 0; i < BYTESPERBLOCK; i++) {
      if (blockBuffer[BYTESPERBLOCK - i - 1] == 0) {
        notRead++;
      } 
      else {
        break;
      }
    }
    tempNumb -= notRead;
  } 
  else {
    i8 wholeBuffer[BYTESPERBLOCK];
    bfsRead(inum, fbn, wholeBuffer);
    memcpy(buf, wholeBuffer, tempNumb);
  }
  fsSeek(fd, tempNumb, SEEK_CUR);
  return tempNumb;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
  // copy data to buffer
  i8 wholeBuffer[numb]; 
  memcpy(wholeBuffer, buf, numb);
  i32 fbn = bfsTell(fd) / BYTESPERBLOCK;
  i32 inum = bfsFdToInum(fd);
  i32 dbn = bfsFbnToDbn(inum, fbn);
  // buffer allocation for the current block
  i8 blockBuffer[BYTESPERBLOCK] = {0};
  // if block doesnt exist then allocate and set it to 0
  if (dbn < 0) {
    bfsAllocBlock(inum, fbn);
    dbn = bfsFbnToDbn(inum, fbn);
    memset(blockBuffer, 0, BYTESPERBLOCK);
  } 
  else {
    bfsRead(inum, fbn, blockBuffer);
  }
  int remainder = numb;
  int currBlockOffset = fsTell(fd) % BYTESPERBLOCK;
  while (remainder > 0) {
    // determining remaining bytes
    int currBytesLeft = BYTESPERBLOCK - currBlockOffset;
    int bytesToWrite;
    if (remainder > currBytesLeft) {
      bytesToWrite = currBytesLeft;
    }     
    else {
      bytesToWrite = remainder;
    }
    memcpy(&blockBuffer[currBlockOffset], &wholeBuffer[numb - remainder], bytesToWrite);
    bioWrite(dbn, blockBuffer);
    remainder -= bytesToWrite;
    currBlockOffset = 0;
    fbn++;
    dbn = bfsFbnToDbn(inum, fbn);
    // block doesnt exist then allocate and set it to 0
    if (dbn < 0) {
      bfsAllocBlock(inum, fbn);
      dbn = bfsFbnToDbn(inum, fbn);
      memset(blockBuffer, 0, BYTESPERBLOCK);
    } 
    else {
      bfsRead(inum, fbn, blockBuffer);
    }
  }
  fsSeek(fd, numb, SEEK_CUR);
  return 0;
}
