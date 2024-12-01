#include "../../include/file/file.h"

#include <string.h>
#include <errno.h>
#include <math.h>

MdResult mdOpenFile(const char *p_filepath, MdFileAccess access, MdFile &file)
{
    if ((access >> 2) >= 3)
    {
        LOG_ERROR("Access flag can only have MD_FILE_ACCESS_CREATE or MD_FILE_ACCESS_APPEND set\n");
        return MD_ERROR_FILE_INVALID_ACCESS_ARGS;
    }

    file.access = access;
    file.pointer = 0;
    file.size = 0;
    
    switch (access & 3)
    {
        case MD_FILE_ACCESS_READ:
        {
            if ((access & MD_FILE_ACCESS_APPEND) > 0)
            {
                LOG_ERROR("MD_FILE_ACCESS_APPEND cannot be set in read-only mode");
                return MD_ERROR_FILE_INVALID_ACCESS_ARGS;
            }

            file.handle = fopen(p_filepath, "r");
            if (file.handle == NULL)
            {
                if ((access & MD_FILE_ACCESS_CREATE) == 0)
                    return MD_ERROR_FILE_NOT_FOUND;
                
                file.handle = fopen(p_filepath, "w");
                if (file.handle == NULL)
                {
                    LOG_ERROR("Failed to create file \"%s\" for reading: %s", p_filepath, strerror(errno));
                    return MD_ERROR_FILE_WRITE_FAILURE;
                }
                fclose(file.handle);

                file.handle = fopen(p_filepath, "r");
                return MD_SUCCESS;
            }

            fseek(file.handle, 0, SEEK_END);
            file.size = ftell(file.handle);
            fseek(file.handle, 0, SEEK_SET);

            return MD_SUCCESS;
        }
        case MD_FILE_ACCESS_WRITE:
        {
            if ((access & MD_FILE_ACCESS_CREATE) == 0)
            {
                file.handle = fopen(p_filepath, "r");
                if (file.handle == NULL)
                    return MD_ERROR_FILE_NOT_FOUND;
                
                fclose(file.handle);
            }

            file.handle = fopen(p_filepath, ((access & MD_FILE_ACCESS_APPEND) > 0) ? "a" : "w");
            
            file.pointer = ftell(file.handle);
            fseek(file.handle, 0, SEEK_END);
            file.size = ftell(file.handle);
            fseek(file.handle, file.pointer, SEEK_SET);
            
            return MD_SUCCESS;
        }
        case MD_FILE_ACCESS_READ | MD_FILE_ACCESS_WRITE:
        {
            switch (access & 0xC)
            {
                case 0: 
                {
                    file.handle = fopen(p_filepath, "r+");
                    if (file.handle == NULL)
                        return MD_ERROR_FILE_NOT_FOUND;
                    
                    fseek(file.handle, 0, SEEK_END);
                    file.size = ftell(file.handle);
                    fseek(file.handle, 0, SEEK_SET);

                    return MD_SUCCESS;
                }
                case MD_FILE_ACCESS_APPEND:
                {
                    file.handle = fopen(p_filepath, "r");
                    if (file.handle == NULL)
                        return MD_ERROR_FILE_NOT_FOUND;
                    
                    fclose(file.handle);
                    file.handle = fopen(p_filepath, "a+");

                    file.pointer = ftell(file.handle);
                    fseek(file.handle, 0, SEEK_END);
                    file.size = ftell(file.handle);
                    fseek(file.handle, file.pointer, SEEK_SET);

                    return MD_SUCCESS;
                }
                default:
                {
                    const char *flags = ((access & MD_FILE_ACCESS_APPEND) > 0) ? "a+" : "w+";
                    file.handle = fopen(p_filepath, flags);
                    
                    file.pointer = ftell(file.handle);
                    fseek(file.handle, 0, SEEK_END);
                    file.size = ftell(file.handle);
                    fseek(file.handle, file.pointer, SEEK_SET);
                    
                    return MD_SUCCESS;
                }
            }
        }
        break;
    }

    return MD_ERROR_FILE_INVALID_ACCESS_ARGS;
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

    if (fseek(file.handle, offset, SEEK_SET) != 0)
    {
        LOG_ERROR("File read error: %s\n", strerror(errno));
        return MD_ERROR_FILE_READ_FAILURE;
    }

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        usize result = fread(p_dst, 1, copy_size, file.handle);
        bytes_written += result;

        if (result < copy_size)
        {
            LOG_ERROR("failed to read entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }
        
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
        usize result = fread(p_dst, 1, copy_size, file.handle);
        bytes_written += result;

        if (result < copy_size)
        {
            LOG_ERROR("failed to read entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }
        
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

    if (fseek(file.handle, offset, SEEK_SET) != 0)
    {
        LOG_ERROR("File read error: %s\n", strerror(errno));
        return MD_ERROR_FILE_READ_FAILURE;
    }

    for (usize b=0; b<block_count; b++)
    {
        usize copy_size = MIN_VAL(current_size, block_size);
        usize result = fwrite(p_src, 1, copy_size, file.handle);
        bytes_written += result;

        if (result < copy_size)
        {
            LOG_ERROR("failed to write entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }
        
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
        usize result = fwrite(p_src, 1, copy_size, file.handle);
        bytes_written += result;

        if (result < copy_size)
        {
            LOG_ERROR("failed to write entire file: %s\n", strerror(errno));
            return MD_ERROR_FILE_READ_FAILURE;
        }
        
        current_size -= block_size;
    }

    if (p_bytes_written != NULL)
        *p_bytes_written = bytes_written;

    return MD_SUCCESS;
}

void mdCloseFile(MdFile &file)
{
    fclose(file.handle);
}
