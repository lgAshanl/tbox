#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <list>
#include <stdio.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include "types.h"

#pragma once

namespace TB
{

    class FileInProgress
    {
    public:
        FileInProgress(std::string& path)
          : m_handle(g_badHandle),
            m_path(path),
            m_size(0)
        {
        }

        ~FileInProgress()
        {
            if (m_handle != g_badHandle)
                {
                    close(m_handle);
                    m_handle = g_badHandle;
                }
        }

        bool open()
        {
            m_handle = ::open(m_path.c_str(), O_RDONLY);
            if (g_badHandle == m_handle)
                {
                    return false;
                }

            struct stat st = {0};
            if (0 != fstat(m_handle, &st))
                {
                    close(m_handle);
                    m_handle = g_badHandle;
                    return false;
                }

            m_size = st.st_size;

            return true;
        }

        bool create(filesize_t size)
        {
            m_handle = ::open(m_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (g_badHandle == m_handle)
                {
                    return false;
                }

            m_size = size;

            return true;
        }

        FileInProgress() = delete;

        FileInProgress(const FileInProgress& other) = delete;
        FileInProgress(FileInProgress&& other) noexcept = delete;
        FileInProgress& operator=(const FileInProgress& other) = delete;
        FileInProgress& operator=(FileInProgress&& other) noexcept = delete;

        bool ReadNextSegment(uint8_t* buf, bufsize_t& size)
        {
            const ssize_t res = read(m_handle, buf, size);
            if (-1 == res)
                {
                    return false;
                }

            size = res;
            return true;
        }

        bool WriteNextSegment(uint8_t* buf, bufsize_t size)
        {
            size_t total_res = 0;

            while (total_res != size)
                {
                    const ssize_t res = write(m_handle, buf, size);
                    if (-1 != res)
                        {
                            total_res += res;
                        }
                }

            m_size += total_res;

            return true;
        }

    private:
        handle_t m_handle;

        std::string m_path;

        filesize_t m_size;

        // filesize_t m_segment;
    };

    class File
    {
    public:
        static bool checkDirExistance(const std::string& dir)
        {
            struct stat info;
            if (0 != stat(dir.c_str(), &info))
                return false;

            if (info.st_mode & S_IFDIR)
                return true;

            return false;
        }

        static bool checkFileExistance(const std::string& path)
        {
            struct stat info;
            if (0 != stat(path.c_str(), &info))
                return 0;

            if (info.st_mode & S_IFREG)
                return true;

            return false;
        }

        static std::string getHomeDir()
        {
            struct passwd* pw = getpwuid(getuid());

            return std::string(pw->pw_dir);
        }

        static bool createdir(const std::string& dir)
        {
            return 0 == mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        }

        static bool removeFile(const std::string& path)
        {
            return std::remove(path.c_str());
        }

        static bool readToBuf(const std::string& path, uint8_t* buf, bufsize_t& size)
        {
            handle_t handle = ::open(path.c_str(), O_RDONLY);
            if (g_badHandle == handle)
            {
                return false;
            }

            struct stat st = {0};
            if (0 != fstat(handle, &st))
            {
                close(handle);
                return false;
            }

            if (size < st.st_size)
            {
                close(handle);
                return false;
            }

            bufsize_t nread = 0;
            while (nread < st.st_size)
            {
                const ssize_t res = read(handle, buf + nread, st.st_size - nread);
                if (-1 == res)
                {
                    close(handle);
                    return false;
                }

                nread += (bufsize_t)res;
            }

            close(handle);

            size = nread;
            return true;
        }

        static bool writeBuf(const std::string& path, const char* buf, bufsize_t size)
        {
            handle_t handle = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (g_badHandle == handle)
            {
                return false;
            }

            bufsize_t nwrite = 0;
            while (nwrite < size)
            {
                const ssize_t res = write(handle, buf + nwrite, size - nwrite);
                if (-1 == res)
                {
                    close(handle);
                    return false;
                }

                nwrite += (bufsize_t)res;
            }

            close(handle);
            return true;
        }

        static std::list<std::string> getFileList(const std::string& root,
                                                  const std::string& directory)
        {
            const std::string fullpath = root + "/" + directory;
            std::list<std::string> file_list;

            DIR* dir = opendir(fullpath.c_str());
            if (NULL != dir)
                {
                    struct dirent* ent;
                    while (NULL != (ent = readdir(dir)))
                        {
                            if ((0 !=
                                 strncmp(".", (char*)&(ent->d_name), 256)) &&
                                (0 !=
                                 strncmp("..", (char*)&(ent->d_name), 256)) &&
                                (0 !=
                                 strncmp(".passw", (char*)&(ent->d_name), 256)))
                                file_list.emplace_back(
                                    std::string(ent->d_name));
                        }
                    closedir(dir);
                }
            else
                {
                    /* could not open directory */
                    perror("dirrerr");
                }

            return file_list;
        }
    };

} // namespace TB
