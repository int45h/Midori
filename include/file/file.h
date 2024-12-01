#pragma once

#include "../typedefs.h"
enum MdFileAccessFlags
{
    MD_FILE_ACCESS_READ = 1,
    MD_FILE_ACCESS_WRITE = 2,
    MD_FILE_ACCESS_CREATE = 4,
    MD_FILE_ACCESS_APPEND = 8,
    MD_FILE_ACCESS_BITS_MAX_ENUM = 0xFF
};

typedef FILE* MdFileHandle;
typedef u8 MdFileAccess;
struct MdFile
{
    MdFileHandle handle;
    MdFileAccess access;
    usize pointer;
    usize size;
};

MdResult mdOpenFile(const char *p_filepath, MdFileAccess access, MdFile &file);
MdResult mdReadFile(MdFile &file, 
                    usize offset, 
                    usize range, 
                    void *p_dst, 
                    usize block_size = 262144,
                    usize *p_bytes_written = NULL);
MdResult mdReadFile(MdFile &file, 
                    usize size,
                    void *p_dst, 
                    usize block_size = 262144,
                    usize *p_bytes_written = NULL);
MdResult mdWriteFile(   MdFile &file, 
                        usize offset, 
                        usize range, 
                        const void *p_src, 
                        usize block_size = 262144,
                        usize *p_bytes_written = NULL);
MdResult mdWriteFile(   MdFile &file, 
                        usize size,
                        const void *p_src, 
                        usize block_size = 262144,
                        usize *p_bytes_written = NULL);
void mdCloseFile(MdFile &file);