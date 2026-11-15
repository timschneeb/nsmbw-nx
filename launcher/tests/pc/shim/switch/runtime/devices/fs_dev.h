#pragma once

#include <switch.h>

#include <cstdio>

constexpr size_t FS_MAX_PATH = 0x301;

struct FsFileSystem;

struct FsFile {
    std::FILE* handle{};
};

enum {
    FsOpenMode_Write = 3,
    FsWriteOption_None = 0,
};

Result fsdevCreateFile(const char* path, size_t size, u32 flags);
Result fsdevDeleteDirectoryRecursively(const char* path);
int fsdevTranslatePath(const char* path, FsFileSystem** device, char* outpath);

Result fsFsOpenFile(FsFileSystem* fs, const char* path, u32 mode, FsFile* out);
Result fsFileSetSize(FsFile* file, s64 size);
Result fsFileWrite(FsFile* file, s64 offset, const void* data, u64 size, u32 option);
void fsFileClose(FsFile* file);
