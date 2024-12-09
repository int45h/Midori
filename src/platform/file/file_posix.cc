#include "../../../include/platform/file/file.h"
#include "include/typedefs.h"

#include <string.h>
#include <errno.h>
#include <math.h>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

struct MdFileDescriptor
{
    int fd;
};

MdResult mdOpenFile(const char *p_filepath, MdFileAccess file_access, MdFile &file)
{
    file.access = file_access;
    file.size = 0;
    
    int fd = -1;
    struct stat st;

    fd = open(p_filepath, file_access);
    if (fd < 0) return MD_ERROR_FILE_READ_FAILURE;
    
    file.p_descriptor = (MdFileDescriptor*)malloc(sizeof(MdFileDescriptor));
    file.p_descriptor->fd = fd;

    stat(p_filepath, &st);
    file.size = st.st_size;

    file.pointer = ((file_access & O_APPEND) > 0) ? st.st_size : 0;
    return MD_SUCCESS;
}

MdResult mdReadFile(MdFile &file, 
                    usize offset, 
                    usize range, 
                    void *p_dst, 
                    usize block_size,
                    usize *p_bytes_written)
{
    if ((offset + range) > file.size)
    {
        LOG_ERROR(
            "Offset + Range (%zu + %zu) cannot be larger than the file's size (%zu)\n",
            offset, 
            range, 
            file.size
        );
        return MD_ERROR_FILE_READ_FAILURE;
    }
    usize bytes_written = 0;
    
    block_size = (block_size > 0) ? block_size : file.size;
    usize block_count = ceil(range / block_size);
    usize current_size = file.size;

    __off_t off = lseek(file.p_descriptor->fd, offset, 0);
    if (off == -1 && errno != 0)
    {
        LOG_ERROR("File read error: %s\n", strerror(errno));
        return MD_ERROR_FILE_READ_FAILURE;
    }

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        ssize result = read(file.p_descriptor->fd, p_dst, copy_size);
        
        if (result < 0)
        {
            LOG_ERROR("failed to read entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }
        
        bytes_written += result;
        current_size -= block_size;
    }

    if (p_bytes_written != NULL)
        *p_bytes_written = bytes_written;

    return MD_SUCCESS;
}

MdResult mdReadFile(MdFile &file, 
                    usize size,
                    void *p_dst, 
                    usize block_size,
                    usize *p_bytes_written)
{
    if (size > file.size)
    {
        LOG_ERROR(
            "Size (%zu) cannot be larger than the file's size (%zu)\n",
            size,
            file.size
        );
        return MD_ERROR_FILE_READ_FAILURE;
    }

    usize bytes_written = 0;
    block_size = (block_size > 0) ? block_size : file.size;
    usize block_count = ceil((float)size / block_size);
    usize current_size = file.size;

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        ssize result = read(file.p_descriptor->fd, p_dst, copy_size);
        
        if (result < 0)
        {
            LOG_ERROR("failed to read entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }

        bytes_written += (usize)result;
        current_size -= block_size;
    }

    if (p_bytes_written != NULL)
        *p_bytes_written = bytes_written;

    return MD_SUCCESS;
}

MdResult mdWriteFile(   MdFile &file, 
                        usize offset, 
                        usize range, 
                        const void *p_src, 
                        usize block_size,
                        usize *p_bytes_written)
{
    usize bytes_written = 0;
    block_size = (block_size > 0) ? block_size : file.size;
    usize block_count = ceil(range / block_size);
    usize current_size = file.size;

    __off_t off = lseek(file.p_descriptor->fd, offset, SEEK_SET);
    if (off == -1 && errno != 0)
    {
        LOG_ERROR("File read error: %s\n", strerror(errno));
        return MD_ERROR_FILE_READ_FAILURE;
    }

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        ssize result = write(file.p_descriptor->fd, p_src, copy_size);
        
        if (result < 0)
        {
            LOG_ERROR("failed to write entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }
        
        bytes_written += (usize)result;
        current_size -= block_size;
    }

    if (p_bytes_written != NULL)
        *p_bytes_written = bytes_written;

    return MD_SUCCESS;
}

MdResult mdWriteFile(   MdFile &file, 
                        usize size,
                        const void *p_src, 
                        usize block_size,
                        usize *p_bytes_written)
{
    usize bytes_written = 0;
    
    block_size = (block_size > 0) ? block_size : file.size;
    usize block_count = ceil(size / block_size);
    usize current_size = file.size;

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        ssize result = write(file.p_descriptor->fd, p_src, copy_size);
        
        if (result < 0)
        {
            LOG_ERROR("failed to write entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }

        bytes_written += (usize)result;
        current_size -= block_size;
    }

    if (p_bytes_written != NULL)
        *p_bytes_written = bytes_written;

    return MD_SUCCESS;
}

void mdCloseFile(MdFile &file)
{
    if (file.p_descriptor != NULL)
    {
        close(file.p_descriptor->fd);
        free(file.p_descriptor);
    }
}
