#pragma once

#include "../../typedefs.h"
#if defined(__GENERIC__)
    enum MdFileAccessFlags
    {
        MD_FILE_ACCESS_READ_ONLY = 1,
        MD_FILE_ACCESS_WRITE_ONLY = 2,
        MD_FILE_ACCESS_READ_WRITE = 3,
        MD_FILE_ACCESS_CREATE = 4,
        MD_FILE_ACCESS_APPEND = 8
    };
#elif defined(__unix__)
    #include <unistd.h>
    #include <fcntl.h>
    enum MdFileAccessFlags
    {
        MD_FILE_ACCESS_READ_ONLY = O_RDONLY,
        MD_FILE_ACCESS_WRITE_ONLY = O_WRONLY,
        MD_FILE_ACCESS_READ_WRITE = O_RDWR,
        MD_FILE_ACCESS_CREATE = O_CREAT,
        MD_FILE_ACCESS_APPEND = O_APPEND
    };
#endif

struct MdFileDescriptor;

typedef FILE* MdFileHandle;
typedef u8 MdFileAccess;
struct MdFile
{
    MdFileDescriptor *p_descriptor;
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