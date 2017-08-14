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
#ifndef FatVolume_h
#define FatVolume_h
#include "FatPartition.h"
#include "FatFile.h"
/**
 * \file
 * \brief FatVolume class
 */
//------------------------------------------------------------------------------
/**
 * \class FatVolume
 * \brief Integration class for the FatLib library.
 */
class FatVolume : public  FatPartition {
 public:
  /**
   * Initialize an FatVolume object.
   * \param[in] blockDev Device block driver.
   * \param[in] part partition to initialize.
   * \return The value true is returned for success and
   * the value false is returned for failure.
   */
  bool begin(BlockDevice* blockDev, uint8_t part = 0) {
    m_blockDev = blockDev;
    if (!(part ? init(part) : init(1) || init(0))) {
      return false;
    }
    m_cwv = this;
    return true;
  }
  /** Change global current working volume to this volume */
  void chvol() {m_cwv = this;}
  /** \return current working volume. */
  static FatVolume* cwv() {return m_cwv;}


#if ENABLE_ARDUINO_FEATURES
   /** List the directory contents of the root directory to Serial.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   */
  void ls(uint8_t flags = 0) {
    ls(&Serial, flags);
  }
  /** List the directory contents of a directory to Serial.
   *
   * \param[in] path directory to list.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   */
  void ls(const char* path, uint8_t flags = 0) {
    ls(&Serial, path, flags);
  }
  //---------------------------------------------------------------------------
  /** open a file
   *
   * \param[in] path location of file to be opened.
   * \param[in] mode open mode flags.
   * \return a File object.
   */
  File open(const String &path, uint8_t mode = O_READ) {
    return open(path.c_str(), mode );
  }
  //---------------------------------------------------------------------------
  /** open a file
   *
   * \param[in] path location of file to be opened.
   * \param[in] mode open mode flags.
   * \return a File object.
   */
  File open(const char *path, uint8_t mode = O_READ) {
    File tmpFile;
    tmpFile.open(this, path, mode);
    return tmpFile;
  }

#endif  // ENABLE_ARDUINO_FEATURES
  //----------------------------------------------------------------------------
  /**
   * Test for the existence of a file.
   *
   * \param[in] path Path of the file to be tested for.
   *
   * \return true if the file exists else false.
   */
  bool exists(const char* path) {
    FatFile tmp;
    return tmp.open(this, path, O_READ);
  }
  //----------------------------------------------------------------------------
  /** List the directory contents of the volume root directory.
   *
   * \param[in] pr Print stream for list.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   */
  void ls(print_t* pr, uint8_t flags = 0) {
    FatFile root;
    root.openRoot(this);
    root.ls(pr, flags);
  }
  //----------------------------------------------------------------------------
  /** List the directory contents of a directory.
   *
   * \param[in] pr Print stream for list.
   *
   * \param[in] path directory to list.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   */
  void ls(print_t* pr, const char* path, uint8_t flags) {
    FatFile dir;
    dir.open(this, path, O_READ);
    dir.ls(pr, flags);
  }
  //----------------------------------------------------------------------------
  /** Make a subdirectory in the volume root directory.
   *
   * \param[in] path A path with a valid name for the subdirectory.
   *
   * \param[in] pFlag Create missing parent directories if true.
   *
   * \return The value true is returned for success and
   * the value false is returned for failure.
   */
  bool mkdir(const char* path, bool pFlag = true) {
    FatFile root;
    FatFile sub;
    return root.openRoot(this) && sub.mkdir(&root, path, pFlag);
  }
  //----------------------------------------------------------------------------
  /** Remove a file from the volume root directory.
   *
   * \param[in] path A path with a valid name for the file.
   *
   * \return The value true is returned for success and
   * the value false is returned for failure.
   */
  bool remove(const char* path) {
    FatFile tmp;
    return tmp.open(this, path, O_WRITE) && tmp.remove();
  }
  //----------------------------------------------------------------------------
  /** Rename a file or subdirectory.
   *
   * \param[in] oldPath Path name to the file or subdirectory to be renamed.
   *
   * \param[in] newPath New path name of the file or subdirectory.
   *
   * The \a newPath object must not exist before the rename call.
   *
   * The file to be renamed must not be open.  The directory entry may be
   * moved and file system corruption could occur if the file is accessed by
   * a file object that was opened before the rename() call.
   *
   * \return The value true is returned for success and
   * the value false is returned for failure.
   */
  bool rename(const char *oldPath, const char *newPath) {
    FatFile root;
    FatFile file;
    return root.openRoot(this) &&
           file.open(this, oldPath, O_READ) && file.rename(&root, newPath);
  }
  //----------------------------------------------------------------------------
  /** Remove a subdirectory from the volume's working directory.
   *
   * \param[in] path A path with a valid name for the subdirectory.
   *
   * The subdirectory file will be removed only if it is empty.
   *
   * \return The value true is returned for success and
   * the value false is returned for failure.
   */
  bool rmdir(const char* path) {
    FatFile sub;
    if (!sub.open(this, path, O_READ)) {
      return false;
    }
    return sub.rmdir();
  }
  //----------------------------------------------------------------------------
  /** Truncate a file to a specified length.  The current file position
   * will be at the new EOF.
   *
   * \param[in] path A path with a valid name for the file.
   * \param[in] length The desired length for the file.
   *
   * \return The value true is returned for success and
   * the value false is returned for failure.
   */
  bool truncate(const char* path, uint32_t length) {
    FatFile file;
    if (!file.open(this, path, O_WRITE)) {
      return false;
    }
    return file.truncate(length);
  }
  /** Wipe all data from the volume. You must reinitialize the volume before
   *  accessing it again.
   * \param[in] pr print stream for status dots.
   * \return true for success else false.
   */
  bool wipe(print_t* pr = nullptr) {
    return FatPartition::wipe(pr);
  }

 private:
  static FatVolume* m_cwv;
};
#endif  // FatVolume_h
