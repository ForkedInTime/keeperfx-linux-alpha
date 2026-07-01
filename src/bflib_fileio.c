/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_fileio.c
 *     File handling routines wrapper.
 * @par Purpose:
 *     Buffer library for file i/o and directory manage routines.
 *     These should be used for all file access in the game.
 * @par Comment:
 *     Wraps standard c file handling routines. You could say this has no purpose,
 *     but here it is anyway.
 * @author   Tomasz Lis
 * @date     10 Feb 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_fileio.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#if !defined(_WIN32)
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#endif

#include "bflib_basics.h"
#include "bflib_datetm.h"

#include "post_inc.h"

/******************************************************************************/

#if !defined(_WIN32)
/** @internal
 *  Finds a file with case-insensitive matching on Unix-like systems.
 *  Searches the directory for a file matching the given name case-insensitively.
 *  @param fname The filename to search for.
 *  @param actual_fname Buffer to store the actual filename found.
 *  @param buflen Size of the actual_fname buffer.
 *  @return Returns 1 if found, 0 otherwise.
 */
static int find_case_insensitive_file(const char *fname, char *actual_fname, size_t buflen)
{
    // Resolve EACH path component case-insensitively (not just the leaf), so a path whose
    // directory segments differ in case from disk (e.g. "mods/MyMod/sound/Foo.wav" vs a real
    // "mods/mymod/sound/foo.wav") still resolves. Each segment first tries an exact match
    // (fast path: correctly-cased paths are unchanged), then a case-insensitive directory scan.
    char resolved[PATH_MAX];
    size_t rlen = 0;
    const char *p = fname;

    // Seed an absolute root if present.
    if (p[0] == '/') {
        resolved[rlen++] = '/';
        p++;
    }
    resolved[rlen] = '\0';

    while (*p != '\0') {
        const char *seg_end = strchr(p, '/');
        size_t seg_len = (seg_end != NULL) ? (size_t)(seg_end - p) : strlen(p);

        if (seg_len == 0) {                 // collapse empty segments ("//")
            if (seg_end == NULL) break;
            p = seg_end + 1;
            continue;
        }
        if (seg_len >= sizeof(resolved)) {
            return 0;
        }
        char seg[PATH_MAX];
        memcpy(seg, p, seg_len);
        seg[seg_len] = '\0';

        // The directory to look inside: the path resolved so far ("." while still empty).
        const char *search_dir = (rlen == 0) ? "." : resolved;

        char matched[PATH_MAX];
        if ((strcmp(seg, ".") == 0) || (strcmp(seg, "..") == 0)) {
            snprintf(matched, sizeof(matched), "%s", seg);   // pass dot segments through
        } else {
            char trial[PATH_MAX];
            if (strcmp(search_dir, "/") == 0) {
                snprintf(trial, sizeof(trial), "/%s", seg);
            } else {
                snprintf(trial, sizeof(trial), "%s/%s", search_dir, seg);
            }
            if (access(trial, F_OK) == 0) {
                snprintf(matched, sizeof(matched), "%s", seg);   // exact case exists
            } else {
                DIR *dir = opendir(search_dir);
                if (dir == NULL) {
                    return 0;
                }
                int found_seg = 0;
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (strcasecmp(entry->d_name, seg) == 0) {
                        snprintf(matched, sizeof(matched), "%s", entry->d_name);
                        found_seg = 1;
                        break;
                    }
                }
                closedir(dir);
                if (!found_seg) {
                    return 0;
                }
            }
        }

        // Append the matched segment to the resolved path.
        if ((rlen > 0) && (resolved[rlen - 1] != '/')) {
            if (rlen + 1 >= sizeof(resolved)) {
                return 0;
            }
            resolved[rlen++] = '/';
        }
        size_t mlen = strlen(matched);
        if (rlen + mlen >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved + rlen, matched, mlen);
        rlen += mlen;
        resolved[rlen] = '\0';

        if (seg_end == NULL) break;
        p = seg_end + 1;
    }

    if ((rlen == 0) || (rlen + 1 > buflen)) {
        return 0;
    }
    snprintf(actual_fname, buflen, "%s", resolved);
    return (access(actual_fname, F_OK) == 0) ? 1 : 0;
}
#endif

short LbFileExists(const char *fname)
{
  if (access(fname, F_OK) == 0) {
    return 1;
  }
#if !defined(_WIN32)
  char actual_fname[PATH_MAX];
  if (find_case_insensitive_file(fname, actual_fname, sizeof(actual_fname))) {
    return 1;
  }
#endif
  return 0;
}

// Resolve a path to the real on-disk one, fixing leaf-name case on case-sensitive
// filesystems. Always writes a usable path into buf and returns it: the original path
// when it exists as-given (the normal case -> no behaviour change), otherwise a
// case-insensitive match if one is found, otherwise the original unchanged. Use this
// before handing a path to a backend that opens files directly (SDL_mixer, ffmpeg,
// std::ifstream) and so bypasses LbFileOpen's case-insensitive fallback.
const char *LbFileCaseInsensitivePath(const char *fname, char *buf, unsigned long buflen)
{
#if !defined(_WIN32)
  if (access(fname, F_OK) != 0) {
    char actual_fname[PATH_MAX];
    if (find_case_insensitive_file(fname, actual_fname, sizeof(actual_fname))) {
      snprintf(buf, buflen, "%s", actual_fname);
      return buf;
    }
  }
#endif
  snprintf(buf, buflen, "%s", fname);
  return buf;
}

int LbFilePosition(TbFileHandle handle)
{
  int result = ftell(handle);
  return result;
}

int create_directory_for_file(const char * fname)
{
  const int size = strlen(fname) + 1;
  char * tmp = (char *) malloc(size);
  const char * separator = strchr(fname, '/');

  while (separator != NULL) {
    memcpy(tmp, fname, separator - fname);
    tmp[separator - fname] = 0;
#if defined(_WIN32)
    if (mkdir(tmp) != 0) {
#else
    if (mkdir(tmp, 0755) != 0) {
#endif
      if (errno != EEXIST) {
        free(tmp);
        return 0;
      }
    }
    separator = strchr(++separator, '/');
  }
  free(tmp);
  return 1;
}

TbFileHandle LbFileOpen(const char *fname, const unsigned char accmode)
{
  unsigned char mode = accmode;
  const char *open_fname = fname;
#if !defined(_WIN32)
  char actual_fname[PATH_MAX];
  int access_rc = access(fname, F_OK);
  // Try to find the file case-insensitively on Unix-like systems
  if (access_rc != 0 && find_case_insensitive_file(fname, actual_fname, sizeof(actual_fname))) {
    open_fname = actual_fname;
  }
#endif

#if !defined(_WIN32)
  int file_exists = (access_rc == 0) || (open_fname != fname);
#else
  int file_exists = LbFileExists(fname);
#endif

  if ( !file_exists )
  {
#ifdef __DEBUG
    LbSyncLog("LbFileOpen: file doesn't exist\n");
#endif
    if ( mode == Lb_FILE_MODE_READ_ONLY )
      return NULL;
    if ( mode == Lb_FILE_MODE_OLD )
      mode = Lb_FILE_MODE_NEW;
  }
/* DISABLED - NOT NEEDED
  if ( mode == Lb_FILE_MODE_NEW )
  {
#ifdef __DEBUG
    LbSyncLog("LbFileOpen: creating file\n");
#endif
    rc = _sopen(fname, _O_WRONLY|_O_CREAT|_O_TRUNC|_O_BINARY, _SH_DENYNO);
    //setmode(rc,_O_TRUNC);
    close(rc);
  }
*/
  TbFileHandle rc = NULL;
  switch (mode)
  {
  case Lb_FILE_MODE_NEW:
    {
#ifdef __DEBUG
      LbSyncLog("LbFileOpen: LBO_CREAT mode\n");
#endif
        if (create_directory_for_file(fname)) {
          rc = fopen(fname, "wb");
        }
    };break;
  case Lb_FILE_MODE_OLD:
    {
#ifdef __DEBUG
        LbSyncLog("LbFileOpen: LBO_RDWR mode\n");
#endif
        rc = fopen(open_fname, "r+b");
    };break;
  case Lb_FILE_MODE_READ_ONLY:
    {
#ifdef __DEBUG
        LbSyncLog("LbFileOpen: LBO_RDONLY mode\n");
#endif
        rc = fopen(open_fname, "rb");
    };break;
  }
#ifdef __DEBUG
  LbSyncLog("LbFileOpen: errno = %d\n", rc, errno);
#endif
  return rc;
}

//Closes a file
int LbFileClose(TbFileHandle handle)
{
  if ( fclose(handle) )
    return -1;
  else
    return 1;
}

/*
 * Checks if the file position indicator is placed at end of the file.
 */
TbBool LbFileEof(TbFileHandle handle)
{
  if (LbFilePosition(handle) >= LbFileLengthHandle(handle))
    return 1;
  return 0;
}

/** Changes position in opened file.
 *
 * @param handle
 * @param offset
 * @param origin
 * @return Returns new file position, or -1 on error.
 */
int LbFileSeek(TbFileHandle handle, long offset, unsigned char origin)
{
  int rc;
  switch (origin)
  {
  case Lb_FILE_SEEK_BEGINNING:
      rc = fseek(handle, offset, SEEK_SET);
      break;
  case Lb_FILE_SEEK_CURRENT:
      rc = fseek(handle, offset, SEEK_CUR);
      break;
  case Lb_FILE_SEEK_END:
      rc = fseek(handle, offset, SEEK_END);
      break;
  default:
      rc = -1;
      break;
  }
  return rc;
}

/**
 * Reads from previously opened disk file.
 *
 * @param handle
 * @param buffer
 * @param len
 * @return Gives amount of bytes read, or -1 on error.
 */
int LbFileRead(TbFileHandle handle, void *buffer, unsigned long len)
{
    return fread(buffer, 1, len, handle);
}

/**
 * Writes data at the operating system level.
 * The number of bytes transmitted is given by len and the data
 * to be transmitted is located at the address specified by buffer.
 * @return Returns the number of bytes (does not include any extra carriage-return
 * characters transmitted) of data transmitted to the file.
*/
long LbFileWrite(TbFileHandle handle, const void *buffer, const unsigned long len)
{
    return fwrite(buffer, 1, len, handle);
}

/**
 * Flushes the file buffers, writing all data immediately.
 * @return Returns 1 on success, 0 on error.
*/
short LbFileFlush(TbFileHandle handle)
{
  return fflush(handle) == 0;
}

long LbFileLengthHandle(TbFileHandle handle)
{
  long pos = ftell(handle);
  fseek(handle, 0, SEEK_END);
  long result = ftell(handle);
  fseek(handle, pos, SEEK_SET);
  return result;
}

//Returns disk size of file
long LbFileLength(const char *fname)
{
  const char *open_fname = fname;
#if !defined(_WIN32)
  char actual_fname[PATH_MAX];
  // Try to find the file case-insensitively on Unix-like systems
  if (access(fname, F_OK) != 0 && find_case_insensitive_file(fname, actual_fname, sizeof(actual_fname))) {
    open_fname = actual_fname;
  }
#endif
  TbFileHandle handle = fopen(open_fname, "rb");
  long result = -1;
  if (handle)
  {
    fseek(handle, 0, SEEK_END);
    result = ftell(handle);
    fclose(handle);
  }
  return result;
}

//Removes a disk file
int LbFileDelete(const char *filename)
{
  const char *del_fname = filename;
#if !defined(_WIN32)
  char actual_fname[PATH_MAX];
  // Try to find the file case-insensitively on Unix-like systems
  if (access(filename, F_OK) != 0 && find_case_insensitive_file(filename, actual_fname, sizeof(actual_fname))) {
    del_fname = actual_fname;
  }
#endif
  int result;
  if ( remove(del_fname) )
    result = -1;
  else
    result = 1;
  return result;
}

int LbDirectoryCurrent(char *buf, unsigned long buflen)
{
//  if ( GetCurrentDirectoryA(buflen, buf) )
  if ( getcwd(buf,buflen) != NULL )
  {
    if ( buf[1] == ':' )
      strcpy(buf, buf+2);
    int len = strlen(buf);
    if ( len>1 )
    {
      if ( buf[len-2] == '\\' )
        buf[len-2] = '\0';
    }
    return 1;
  }
  return -1;
}

int LbFileMakeFullPath(const short append_cur_dir,
  const char *directory, const char *filename, char *buf, const unsigned long len)
{
  if (filename==NULL)
    { buf[0]='\0'; return -1; }
  unsigned long namestart;
  if ( append_cur_dir )
  {
    if ( LbDirectoryCurrent(buf, len-2) == -1 )
    { buf[0]='\0'; return -1; }
    namestart = strlen(buf);
    if ( (namestart>0) && (buf[namestart-1]!='\\') && (buf[namestart-1]!='/'))
    {
      buf[namestart] = '/';
      namestart++;
    }
  } else
  {
    namestart = 0;
  }
  buf[namestart] = '\0';

  if ( directory != NULL )
  {
      int copy_len = strlen(directory);
      if (len - 2 <= namestart + copy_len - 1)
          return -1;
      memcpy(buf + namestart, directory, copy_len);
      namestart += copy_len - 1;
      if ((namestart > 0) && (buf[namestart - 1] != '\\') && (buf[namestart - 1] != '/'))
      {
          buf[namestart] = '/';
          namestart++;
    }
    buf[namestart] = '\0';
  }
  if ( strlen(filename)+namestart-1 < len )
  {
    const char *ptr = filename;
    int invlen;
    for (invlen=-1;invlen!=0;invlen--)
    {
     if (*ptr++ == 0)
       {invlen--;break;}
    }
    int copy_len = ~invlen;
    const char* copy_src = &ptr[-copy_len];
    char* copy_dst = buf;
    for (invlen=-1;invlen!=0;invlen--)
    {
     if (*copy_dst++ == 0)
       {invlen--;break;}
    }
    memcpy(copy_dst-1, copy_src, copy_len);
    return 1;
  }
  return -1;
}

/******************************************************************************/
