/* FatLib Library
 * Copyright (C) 2012..2017 by William Greiman
 *
 * This file is part of the FatLib Library
 *
 * This Library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the FatLib Library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include "../common/DebugMacros.h"
#include "FatFile.h"
#include "FatVolume.h"
//------------------------------------------------------------------------------
// Add a cluster to a file.
bool FatFile::addCluster() {
  m_flags |= F_FILE_DIR_DIRTY;
  return m_part->allocateCluster(m_curCluster, &m_curCluster);
}
//------------------------------------------------------------------------------
// Add a cluster to a directory file and zero the cluster.
// Return with first sector of cluster in the cache.
bool FatFile::addDirCluster() {
  uint32_t sector;
  cache_t* pc;

  if (isRootFixed()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // max folder size
  if (m_curPosition >= 512UL*4095) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (!addCluster()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  sector = m_part->clusterStartSector(m_curCluster);
  pc = m_part->cacheFetchData(sector, FatCache::CACHE_RESERVE_FOR_WRITE);
  if (!pc) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  memset(pc, 0, m_part->bytesPerSector());
  // zero rest of clusters
  for (uint8_t i = 1; i < m_part->sectorsPerCluster(); i++) {
    if (!m_part->writeSector(sector + i, pc->data)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  // Set position to EOF to avoid inconsistent curCluster/curPosition.
  m_curPosition += m_part->bytesPerCluster();
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
// cache a file's directory entry
// return pointer to cached entry or null for failure
dir_t* FatFile::cacheDirEntry(uint8_t action) {
  cache_t* pc;
  pc = m_part->cacheFetchData(m_dirSector, action);
  if (!pc) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return pc->dir + (m_dirIndex & 0XF);

fail:
  return nullptr;
}
//------------------------------------------------------------------------------
bool FatFile::close() {
  bool rtn = sync();
  m_attr = FILE_ATTR_CLOSED;
  return rtn;
}
//------------------------------------------------------------------------------
bool FatFile::contiguousRange(uint32_t* bgnSector, uint32_t* endSector) {
  // error if no clusters
  if (m_firstCluster == 0) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  for (uint32_t c = m_firstCluster; ; c++) {
    uint32_t next;
    int8_t fg = m_part->fatGet(c, &next);
    if (fg < 0) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    // check for contiguous
    if (fg == 0 || next != (c + 1)) {
      // error if not end of chain
      if (fg) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      *bgnSector = m_part->clusterStartSector(m_firstCluster);
      *endSector = m_part->clusterStartSector(c)
                  + m_part->sectorsPerCluster() - 1;
      return true;
    }
  }

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::preAllocate(uint32_t length) {
  uint32_t need;
  if (!length || !isFile() || !(m_flags & O_WRITE) || m_firstCluster) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  need = 1 + ((length - 1) >> m_part->bytesPerClusterShift());
  // allocate clusters
  if (!m_part->allocContiguous(need, &m_firstCluster)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  m_fileSize = length;

  // insure sync() will update dir entry
  m_flags |= F_FILE_DIR_DIRTY;
  return sync();

 fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::dirEntry(dir_t* dst) {
  dir_t* dir;
  // Make sure fields on device are correct.
  if (!sync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // read entry
  dir = cacheDirEntry(FatCache::CACHE_FOR_READ);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // copy to caller's struct
  memcpy(dst, dir, sizeof(dir_t));
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
int FatFile::fgets(char* str, int num, char* delim) {
  char ch;
  int n = 0;
  int r = -1;
  while ((n + 1) < num && (r = read(&ch, 1)) == 1) {
    // delete CR
    if (ch == '\r') {
      continue;
    }
    str[n++] = ch;
    if (!delim) {
      if (ch == '\n') {
        break;
      }
    } else {
      if (strchr(delim, ch)) {
        break;
      }
    }
  }
  if (r < 0) {
    // read error
    return -1;
  }
  str[n] = '\0';
  return n;
}
//------------------------------------------------------------------------------
void FatFile::fgetpos(fspos_t* pos) {
  pos->position = m_curPosition;
  pos->cluster = m_curCluster;
}
//------------------------------------------------------------------------------
void FatFile::fsetpos(fspos_t* pos) {
  m_curPosition = pos->position;
  m_curCluster = pos->cluster;
}
//------------------------------------------------------------------------------
bool FatFile::mkdir(FatFile* parent, const char* path, bool pFlag) {
  fname_t fname;
  FatFile tmpDir;

  if (isOpen() || !parent->isDir()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (isDirSeparator(*path)) {
    while (isDirSeparator(*path)) {
      path++;
    }
    if (!tmpDir.openRoot(parent->m_part)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    parent = &tmpDir;
  }
  while (1) {
    if (!parsePathName(path, &fname, &path)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (!*path) {
      break;
    }
    if (!open(parent, &fname, O_READ)) {
      if (!pFlag || !mkdir(parent, &fname)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
    tmpDir = *this;
    parent = &tmpDir;
    close();
  }
  return mkdir(parent, &fname);

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::mkdir(FatFile* parent, fname_t* fname) {
  uint32_t sector;
  dir_t dot;
  dir_t* dir;
  cache_t* pc;

  if (!parent->isDir()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // create a normal file
  if (!open(parent, fname, O_CREAT | O_EXCL | O_RDWR)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // convert file to directory
  m_flags = O_READ;
  m_attr = FILE_ATTR_SUBDIR;

  // allocate and zero first cluster
  if (!addDirCluster()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  m_firstCluster = m_curCluster;
  // Set to start of dir
  rewind();
  // force entry to device
  if (!sync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // cache entry - should already be in cache due to sync() call
  dir = cacheDirEntry(FatCache::CACHE_FOR_WRITE);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // change directory entry  attribute
  dir->attributes = FAT_ATTRIB_DIRECTORY;

  // make entry for '.'
  memcpy(&dot, dir, sizeof(dot));
  dot.name[0] = '.';
  for (uint8_t i = 1; i < 11; i++) {
    dot.name[i] = ' ';
  }

  // cache sector for '.'  and '..'
  sector = m_part->clusterStartSector(m_firstCluster);
  pc = m_part->cacheFetchData(sector, FatCache::CACHE_FOR_WRITE);
  if (!pc) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // copy '.' to sector
  memcpy(&pc->dir[0], &dot, sizeof(dot));
  // make entry for '..'
  dot.name[1] = '.';
  setLe16(dot.firstClusterLow, parent->m_firstCluster & 0XFFFF);
  setLe16(dot.firstClusterHigh, parent->m_firstCluster >> 16);
  // copy '..' to sector
  memcpy(&pc->dir[1], &dot, sizeof(dot));
  // write first sector
  return m_part->cacheSync();

fail:
  return false;
}
//-----------------------------------------------------------------------------
bool FatFile::open(const char* path, uint8_t oflag) {
  return FatVolume::cwv() && open(FatVolume::cwv(), path, oflag);
}
//------------------------------------------------------------------------------
bool FatFile::open(FatVolume* vol, const char* path, uint8_t oflag) {
  FatFile root;
  return root.openRoot(vol) && open(&root, path, oflag);
}
//------------------------------------------------------------------------------
bool FatFile::open(FatFile* dirFile, const char* path, uint8_t oflag) {
  FatFile tmpDir;
  fname_t fname;

  // error if already open
  if (isOpen() || !dirFile->isDir()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (isDirSeparator(*path)) {
    while (isDirSeparator(*path)) {
      path++;
    }
    if (*path == 0) {
      return openRoot(dirFile->m_part);
    }
    if (!tmpDir.openRoot(dirFile->m_part)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    dirFile = &tmpDir;
  }
  while (1) {
    if (!parsePathName(path, &fname, &path)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (*path == 0) {
      break;
    }
    if (!open(dirFile, &fname, O_READ)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    tmpDir = *this;
    dirFile = &tmpDir;
    close();
  }
  return open(dirFile, &fname, oflag);

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::open(FatFile* dirFile, uint16_t index, uint8_t oflag) {
  uint8_t checksum = 0;
  uint8_t lfnOrd = 0;
  dir_t* dir;
  ldir_t* ldir;

  // Error if already open.
  if (isOpen() || !dirFile->isDir()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Don't open existing file if O_EXCL - user call error.
  if (oflag & O_EXCL) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (index) {
    // Check for LFN.
    if (!dirFile->seekSet(32UL*(index -1))) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    ldir = reinterpret_cast<ldir_t*>(dirFile->readDirCache());
    if (!ldir) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (ldir->attributes == FAT_ATTRIB_LONG_NAME) {
      if (1 == (ldir->order & 0X1F)) {
        checksum = ldir->checksum;
        // Use largest possible number.
        lfnOrd = index > 20 ? 20 : index;
      }
    }
  } else {
    dirFile->rewind();
  }
  // read entry into cache
  dir = dirFile->readDirCache();
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // error if empty slot or '.' or '..'
  if (dir->name[0] == FAT_NAME_DELETED ||
      dir->name[0] == FAT_NAME_FREE ||
      dir->name[0] == '.') {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (lfnOrd && checksum != lfnChecksum(dir->name)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // open cached entry
  if (!openCachedEntry(dirFile, index, oflag, lfnOrd)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
// open a cached directory entry.

bool FatFile::openCachedEntry(FatFile* dirFile, uint16_t dirIndex,
                              uint8_t oflag, uint8_t lfnOrd) {
  uint32_t firstCluster;
  memset(this, 0, sizeof(FatFile));
  // location of entry in cache
  m_part = dirFile->m_part;
  m_dirIndex = dirIndex;
  m_dirCluster = dirFile->m_firstCluster;
  dir_t* dir = reinterpret_cast<dir_t*>(m_part->cacheAddress());
  dir += 0XF & dirIndex;

  // Must be file or subdirectory.
  if (!isFileOrSubdir(dir)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  m_attr = dir->attributes & FILE_ATTR_COPY;
  if (isFileDir(dir)) {
    m_attr |= FILE_ATTR_FILE;
  }
  m_lfnOrd = lfnOrd;
  // Write, truncate, or at end is an error for a directory or read-only file.
  if (oflag & (O_WRITE | O_TRUNC | O_AT_END)) {
    if (isSubDir() || isReadOnly()) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  // save open flags for read/write
  m_flags = oflag & F_OFLAG;

  m_dirSector = m_part->cacheSectorNumber();

  // copy first cluster number for directory fields
  firstCluster = ((uint32_t)getLe16(dir->firstClusterHigh) << 16)
                 | getLe16(dir->firstClusterLow);

  if (oflag & O_TRUNC) {
    if (!(oflag & O_WRITE)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (firstCluster && !m_part->freeChain(firstCluster)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    // need to update directory entry
    m_flags |= F_FILE_DIR_DIRTY;
  } else {
    m_firstCluster = firstCluster;
    m_fileSize = getLe32(dir->fileSize);
  }
  if ((oflag & O_AT_END) && !seekSet(m_fileSize)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return true;

fail:
  m_attr = FILE_ATTR_CLOSED;
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::openNext(FatFile* dirFile, uint8_t oflag) {
  uint8_t checksum = 0;
  ldir_t* ldir;
  uint8_t lfnOrd = 0;
  uint16_t index;

  // Check for not open and valid directory..
  if (isOpen() || !dirFile->isDir() || (dirFile->curPosition() & 0X1F)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  while (1) {
    // read entry into cache
    index = dirFile->curPosition()/32;
    dir_t* dir = dirFile->readDirCache();
    if (!dir) {
      if (dirFile->getError()) {
        DBG_FAIL_MACRO;
      }
      goto fail;
    }
    // done if last entry
    if (dir->name[0] == FAT_NAME_FREE) {
      goto fail;
    }
    // skip empty slot or '.' or '..'
    if (dir->name[0] == '.' || dir->name[0] == FAT_NAME_DELETED) {
      lfnOrd = 0;
    } else if (isFileOrSubdir(dir)) {
      if (lfnOrd && checksum != lfnChecksum(dir->name)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      if (!openCachedEntry(dirFile, index, oflag, lfnOrd)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      return true;
    } else if (isLongName(dir)) {
      ldir = reinterpret_cast<ldir_t*>(dir);
      if (ldir->order & FAT_ORDER_LAST_LONG_ENTRY) {
        lfnOrd = ldir->order & 0X1F;
        checksum = ldir->checksum;
      }
    } else {
      lfnOrd = 0;
    }
  }

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::openRoot(FatPartition* vol) {
  // error if file is already open
  if (isOpen()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  memset(this, 0, sizeof(FatFile));

  m_part = vol;
  switch (vol->fatType()) {
#if FAT12_SUPPORT
  case 12:
#endif  // FAT12_SUPPORT
  case 16:
    m_attr = FILE_ATTR_ROOT_FIXED;
    break;

  case 32:
    m_attr = FILE_ATTR_ROOT32;
    break;

  default:
    DBG_FAIL_MACRO;
    goto fail;
  }
  // read only
  m_flags = O_READ;
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
int FatFile::peek() {
  uint64_t curPosition = m_curPosition;
  uint32_t curCluster = m_curCluster;
  int c = read();
  m_curPosition = curPosition;
  m_curCluster = curCluster;
  return c;
}
//------------------------------------------------------------------------------
int FatFile::read(void* buf, size_t nbyte) {
  int8_t fg;
  uint8_t sectorOfCluster = 0;
  uint8_t* dst = reinterpret_cast<uint8_t*>(buf);
  uint16_t offset;
  size_t toRead;
  uint32_t sector;  // raw device sector number
  cache_t* pc;

  // error if not open for read
  if (!isOpen() || !(m_flags & O_READ)) {
    DBG_FAIL_MACRO;
    goto fail;
  }

  if (isFile()) {
    uint32_t tmp32 = m_fileSize - m_curPosition;
    if (nbyte >= tmp32) {
      nbyte = tmp32;
    }
  } else if (isRootFixed()) {
    uint16_t tmp16 = 32*m_part->m_rootDirEntryCount - (uint16_t)m_curPosition;
    if (nbyte > tmp16) {
      nbyte = tmp16;
    }
  }
  toRead = nbyte;
  while (toRead) {
    size_t n;
    offset = m_curPosition & m_part->sectorMask();  // offset in sector
    if (isRootFixed()) {
      sector = m_part->rootDirStart()
               + (m_curPosition >> m_part->bytesPerSectorShift());
    } else {
      sectorOfCluster = m_part->sectorOfCluster(m_curPosition);
      if (offset == 0 && sectorOfCluster == 0) {
        // start of new cluster
        if (m_curPosition == 0) {
          // use first cluster in file
          m_curCluster = isRoot32() ? m_part->rootDirStart() : m_firstCluster;
        } else {
          // get next cluster from FAT
          fg = m_part->fatGet(m_curCluster, &m_curCluster);
          if (fg < 0) {
            DBG_FAIL_MACRO;
            goto fail;
          }
          if (fg == 0) {
            if (isDir()) {
              break;
            }
            DBG_FAIL_MACRO;
            goto fail;
          }
        }
      }
      sector = m_part->clusterStartSector(m_curCluster) + sectorOfCluster;
    }
    if (offset != 0 || toRead < m_part->bytesPerSector()
        || sector == m_part->cacheSectorNumber()) {
      // amount to be read from current sector
      n = m_part->bytesPerSector() - offset;
      if (n > toRead) {
        n = toRead;
      }
      // read sector to cache and copy data to caller
      pc = m_part->cacheFetchData(sector, FatCache::CACHE_FOR_READ);
      if (!pc) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      uint8_t* src = pc->data + offset;
      memcpy(dst, src, n);
#if USE_MULTI_SECTOR_IO
    } else if (toRead >= 2*m_part->bytesPerSector()) {
      uint8_t ns = toRead >> m_part->bytesPerSectorShift();
      if (!isRootFixed()) {
        uint8_t mb = m_part->sectorsPerCluster() - sectorOfCluster;
        if (mb < ns) {
          ns = mb;
        }
      }
      n = ns << m_part->bytesPerSectorShift();
      if (sector <= m_part->cacheSectorNumber()
          && sector < (m_part->cacheSectorNumber() + ns)) {
        // flush cache if a sector is in the cache
        if (!m_part->cacheSyncData()) {
          DBG_FAIL_MACRO;
          goto fail;
        }
      }
      if (!m_part->readSectors(sector, dst, ns)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
#endif  // USE_MULTI_SECTOR_IO
    } else {
      // read single sector
      n = m_part->bytesPerSector();
      if (!m_part->readSector(sector, dst)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
    dst += n;
    m_curPosition += n;
    toRead -= n;
  }
  return nbyte - toRead;

fail:
  m_error |= READ_ERROR;
  return -1;
}
//------------------------------------------------------------------------------
int8_t FatFile::readDir(dir_t* dir) {
  int16_t n;
  // if not a directory file or miss-positioned return an error
  if (!isDir() || (0X1F & m_curPosition)) {
    return -1;
  }

  while (1) {
    n = read(dir, sizeof(dir_t));
    if (n != sizeof(dir_t)) {
      return n == 0 ? 0 : -1;
    }
    // last entry if FAT_NAME_FREE
    if (dir->name[0] == FAT_NAME_FREE) {
      return 0;
    }
    // skip empty entries and entry for .  and ..
    if (dir->name[0] == FAT_NAME_DELETED || dir->name[0] == '.') {
      continue;
    }
    // return if normal file or subdirectory
    if (isFileOrSubdir(dir)) {
      return n;
    }
  }
}
//------------------------------------------------------------------------------
// Read next directory entry into the cache
// Assumes file is correctly positioned
dir_t* FatFile::readDirCache(bool skipReadOk) {
  uint8_t i = (m_curPosition >> 5) & 0XF;

  if (i == 0 || !skipReadOk) {
    int8_t n = read(&n, 1);
    if  (n != 1) {
      if (n != 0) {
        DBG_FAIL_MACRO;
      }
      goto fail;
    }
    m_curPosition += 31;
  } else {
    m_curPosition += 32;
  }
  // return pointer to entry
  return reinterpret_cast<dir_t*>(m_part->cacheAddress()) + i;

fail:
  return nullptr;
}
//------------------------------------------------------------------------------
bool FatFile::remove(const char* path) {
  FatFile file;
  if (!file.open(this, path, O_WRITE)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return file.remove();

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::rename(FatFile* dirFile, const char* newPath) {
  dir_t entry;
  uint32_t dirCluster = 0;
  FatFile file;
  FatFile oldFile;
  cache_t* pc;
  dir_t* dir;

  // Must be an open file or subdirectory.
  if (!(isFile() || isSubDir())) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Can't rename LFN in 8.3 mode.
  if (!USE_LONG_FILE_NAMES && isLFN()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Can't move file to new volume.
  if (m_part != dirFile->m_part) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // sync() and cache directory entry
  sync();
  oldFile = *this;
  dir = cacheDirEntry(FatCache::CACHE_FOR_READ);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // save directory entry
  memcpy(&entry, dir, sizeof(entry));
  // make directory entry for new path
  if (isFile()) {
    if (!file.open(dirFile, newPath, O_CREAT | O_EXCL | O_WRITE)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  } else {
    // don't create missing path prefix components
    if (!file.mkdir(dirFile, newPath, false)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    // save cluster containing new dot dot
    dirCluster = file.m_firstCluster;
  }
  // change to new directory entry

  m_dirSector = file.m_dirSector;
  m_dirIndex = file.m_dirIndex;
  m_lfnOrd = file.m_lfnOrd;
  m_dirCluster = file.m_dirCluster;
  // mark closed to avoid possible destructor close call
  file.m_attr = FILE_ATTR_CLOSED;

  // cache new directory entry
  dir = cacheDirEntry(FatCache::CACHE_FOR_WRITE);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // copy all but name and name flags to new directory entry
  memcpy(&dir->creationTimeTenths, &entry.creationTimeTenths,
         sizeof(entry) - sizeof(dir->name) - 2);
  dir->attributes = entry.attributes;

  // update dot dot if directory
  if (dirCluster) {
    // get new dot dot
    uint32_t sector = m_part->clusterStartSector(dirCluster);
    pc = m_part->cacheFetchData(sector, FatCache::CACHE_FOR_READ);
    if (!pc) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    memcpy(&entry, &pc->dir[1], sizeof(entry));

    // free unused cluster
    if (!m_part->freeChain(dirCluster)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    // store new dot dot
    sector = m_part->clusterStartSector(m_firstCluster);
    pc = m_part->cacheFetchData(sector, FatCache::CACHE_FOR_WRITE);
    if (!pc) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    memcpy(&pc->dir[1], &entry, sizeof(entry));
  }
  // Remove old directory entry;
  oldFile.m_firstCluster = 0;
  oldFile.m_flags = O_WRITE;
  oldFile.m_attr = FILE_ATTR_FILE;
  if (!oldFile.remove()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return m_part->cacheSync();

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::rmdir() {
  // must be open subdirectory
  if (!isSubDir() || (!USE_LONG_FILE_NAMES && isLFN())) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  rewind();

  // make sure directory is empty
  while (1) {
    dir_t* dir = readDirCache(true);
    if (!dir) {
      // EOF if no error.
      if (!getError()) {
        break;
      }
      DBG_FAIL_MACRO;
      goto fail;
    }
    // done if past last used entry
    if (dir->name[0] == FAT_NAME_FREE) {
      break;
    }
    // skip empty slot, '.' or '..'
    if (dir->name[0] == FAT_NAME_DELETED || dir->name[0] == '.') {
      continue;
    }
    // error not empty
    if (isFileOrSubdir(dir)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  // convert empty directory to normal file for remove
  m_attr = FILE_ATTR_FILE;
  m_flags |= O_WRITE;
  return remove();

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::rmRfStar() {
  uint16_t index;
  FatFile f;
  if (!isDir()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  rewind();
  while (1) {
    // remember position
    index = m_curPosition/32;

    dir_t* dir = readDirCache();
    if (!dir) {
      // At EOF if no error.
      if (!getError()) {
        break;
      }
      DBG_FAIL_MACRO;
      goto fail;
    }
    // done if past last entry
    if (dir->name[0] == FAT_NAME_FREE) {
      break;
    }

    // skip empty slot or '.' or '..'
    if (dir->name[0] == FAT_NAME_DELETED || dir->name[0] == '.') {
      continue;
    }

    // skip if part of long file name or volume label in root
    if (!isFileOrSubdir(dir)) {
      continue;
    }

    if (!f.open(this, index, O_READ)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (f.isSubDir()) {
      // recursively delete
      if (!f.rmRfStar()) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    } else {
      // ignore read-only
      f.m_flags |= O_WRITE;
      if (!f.remove()) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
    // position to next entry if required
    if (m_curPosition != (32UL*(index + 1))) {
      if (!seekSet(32UL*(index + 1))) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
  }
  // don't try to delete root
  if (!isRoot()) {
    if (!rmdir()) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::seekSet(uint32_t pos) {
  uint32_t nCur;
  uint32_t nNew;
  uint32_t tmp = m_curCluster;
  // error if file not open
  if (!isOpen()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Optimize O_APPEND writes.
  if (pos == m_curPosition) {
    return true;
  }
  if (pos == 0) {
    // set position to start of file
    m_curCluster = 0;
    goto done;
  }
  if (isFile()) {
    if (pos > m_fileSize) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  } else if (isRootFixed()) {
    if (pos <= 32*m_part->rootDirEntryCount()) {
      goto done;
    }
    DBG_FAIL_MACRO;
    goto fail;
  }
  // calculate cluster index for cur and new position
  nCur = (m_curPosition - 1) >> (m_part->bytesPerClusterShift());
  nNew = (pos - 1) >> (m_part->bytesPerClusterShift());

  if (nNew < nCur || m_curPosition == 0) {
    // must follow chain from first cluster
    m_curCluster = isRoot32() ? m_part->rootDirStart() : m_firstCluster;
  } else {
    // advance from curPosition
    nNew -= nCur;
  }
  while (nNew--) {
    if (m_part->fatGet(m_curCluster, &m_curCluster) <= 0) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }

done:
  m_curPosition = pos;
  return true;

fail:
  m_curCluster = tmp;
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::sync() {
  uint16_t date, time;
  if (!isOpen()) {
    return true;
  }
  if (m_flags & F_FILE_DIR_DIRTY) {
    dir_t* dir = cacheDirEntry(FatCache::CACHE_FOR_WRITE);
    // check for deleted by another open file object
    if (!dir || dir->name[0] == FAT_NAME_DELETED) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    // do not set filesize for dir files
    if (isFile()) {
      setLe32(dir->fileSize, m_fileSize);
    }

    // update first cluster fields
    setLe16(dir->firstClusterLow, m_firstCluster & 0XFFFF);
    setLe16(dir->firstClusterHigh, m_firstCluster >> 16);

    // set modify time if user supplied a callback date/time function
    if (FsDateTime::callback) {
      FsDateTime::callback(&date, &time);
      setLe16(dir->modifyDate, date);
      setLe16(dir->accessDate, date);
      setLe16(dir->modifyTime, time);
    }
    // clear directory dirty
    m_flags &= ~F_FILE_DIR_DIRTY;
  }
  if (m_part->cacheSync()) {
    return true;
  }
  DBG_FAIL_MACRO;

fail:
  m_error |= WRITE_ERROR;
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::timestamp(uint8_t flags, uint16_t year, uint8_t month,
                   uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  uint16_t dirDate;
  uint16_t dirTime;
  dir_t* dir;

  if (!isFile()
      || year < 1980
      || year > 2107
      || month < 1
      || month > 12
      || day < 1
      || day > 31
      || hour > 23
      || minute > 59
      || second > 59) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // update directory entry
  if (!sync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  dir = cacheDirEntry(FatCache::CACHE_FOR_WRITE);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  dirDate = FS_DATE(year, month, day);
  dirTime = FS_TIME(hour, minute, second);
  if (flags & T_ACCESS) {
    setLe16(dir->accessDate, dirDate);
  }
  if (flags & T_CREATE) {
    setLe16(dir->createDate, dirDate);
    setLe16(dir->createTime, dirTime);
    // seems to be units of 1/100 second not 1/10 as Microsoft states
    dir->creationTimeTenths = second & 1 ? 100 : 0;
  }
  if (flags & T_WRITE) {
    setLe16(dir->modifyDate, dirDate);
    setLe16(dir->modifyTime, dirTime);
  }
  return m_part->cacheSync();

fail:
  return false;
}
//------------------------------------------------------------------------------
bool FatFile::truncate() {
  uint32_t toFree;
  // error if not a normal file or read-only
  if (!isFile() || !(m_flags & O_WRITE)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (m_firstCluster == 0) {
      return true;
  }
  if (m_curCluster) {
    toFree = 0;
    int8_t fg = m_part->fatGet(m_curCluster, &toFree);
    if (fg < 0) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (fg) {
      // current cluster is end of chain
      if (!m_part->fatPutEOC(m_curCluster)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
  } else {
    toFree = m_firstCluster;
    m_firstCluster = 0;
  }
  if (toFree) {
    if (!m_part->freeChain(toFree)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  m_fileSize = m_curPosition;

  // need to update directory entry
  m_flags |= F_FILE_DIR_DIRTY;

  if (!sync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return true;

 fail:
  return false;
}
//------------------------------------------------------------------------------
size_t FatFile::write(const void* buf, size_t nbyte) {
  // convert void* to uint8_t*  -  must be before goto statements
  const uint8_t* src = reinterpret_cast<const uint8_t*>(buf);
  cache_t* pc;
  uint8_t cacheOption;
  // number of bytes left to write  -  must be before goto statements
  size_t nToWrite = nbyte;
  size_t n;
  // error if not a normal file or is read-only
  if (!isFile() || !(m_flags & O_WRITE)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // seek to end of file if append flag
  if ((m_flags & O_APPEND)) {
    if (!seekSet(m_fileSize)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  // Don't exceed max fileSize.
  if (nbyte > (0XFFFFFFFF - m_curPosition)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  while (nToWrite) {
    uint8_t sectorOfCluster = m_part->sectorOfCluster(m_curPosition);
    uint16_t sectorOffset = m_curPosition & m_part->sectorMask();
    if (sectorOfCluster == 0 && sectorOffset == 0) {
      // start of new cluster
      if (m_curCluster != 0) {
        int8_t fg = m_part->fatGet(m_curCluster, &m_curCluster);
        if (fg < 0) {
          DBG_FAIL_MACRO;
          goto fail;
        }
        if (fg == 0) {
          // add cluster if at end of chain
          if (!addCluster()) {
            DBG_FAIL_MACRO;
            goto fail;
          }
        }
      } else {
        if (m_firstCluster == 0) {
          // allocate first cluster of file
          if (!addCluster()) {
            DBG_FAIL_MACRO;
            goto fail;
          }
          m_firstCluster = m_curCluster;
        } else {
          m_curCluster = m_firstCluster;
        }
      }
    }
    // sector for data write
    uint32_t sector = m_part->clusterStartSector(m_curCluster)
                      + sectorOfCluster;

    if (sectorOffset != 0 || nToWrite < m_part->bytesPerSector()) {
      // partial sector - must use cache
      // max space in sector
      n = m_part->bytesPerSector() - sectorOffset;
      // lesser of space and amount to write
      if (n > nToWrite) {
        n = nToWrite;
      }

      if (sectorOffset == 0 && m_curPosition >= m_fileSize) {
        // start of new sector don't need to read into cache
        cacheOption = FatCache::CACHE_RESERVE_FOR_WRITE;
      } else {
        // rewrite part of sector
        cacheOption = FatCache::CACHE_FOR_WRITE;
      }
      pc = m_part->cacheFetchData(sector, cacheOption);
      if (!pc) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      uint8_t* dst = pc->data + sectorOffset;
      memcpy(dst, src, n);
      if (m_part->bytesPerSector() == (n + sectorOffset)) {
        // Force write if sector is full - improves large writes.
        if (!m_part->cacheSyncData()) {
          DBG_FAIL_MACRO;
          goto fail;
        }
      }
#if USE_MULTI_SECTOR_IO
    } else if (nToWrite >= 2*m_part->bytesPerSector()) {
      // use multiple sector write command
      uint8_t maxSectors = m_part->sectorsPerCluster() - sectorOfCluster;
      uint8_t nSector = nToWrite >> m_part->bytesPerSectorShift();
      if (nSector > maxSectors) {
        nSector = maxSectors;
      }
      n = nSector << m_part->bytesPerSectorShift();
      if (sector <= m_part->cacheSectorNumber()
          && sector < (m_part->cacheSectorNumber() + nSector)) {
        // invalidate cache if sector is in cache
        m_part->cacheInvalidate();
      }
      if (!m_part->writeSectors(sector, src, nSector)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
#endif  // USE_MULTI_SECTOR_IO
    } else {
      // use single sector write command
      n = m_part->bytesPerSector();
      if (m_part->cacheSectorNumber() == sector) {
        m_part->cacheInvalidate();
      }
      if (!m_part->writeSector(sector, src)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
    m_curPosition += n;
    src += n;
    nToWrite -= n;
  }
  if (m_curPosition > m_fileSize) {
    // update fileSize and insure sync will update dir entry
    m_fileSize = m_curPosition;
    m_flags |= F_FILE_DIR_DIRTY;
  } else if (FsDateTime::callback) {
    // insure sync will update modified date and time
    m_flags |= F_FILE_DIR_DIRTY;
  }

  if (m_flags & O_SYNC) {
    if (!sync()) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  return nbyte;

fail:
  // return for write error
  m_error |= WRITE_ERROR;
  return -1;
}
