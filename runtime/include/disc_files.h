#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wii_disc.h"

// The player's copy of the game: either a disc image (.iso, .wbfs, .ciso, .wia, .rvz) or a folder
// Dolphin's Extract Entire Disc made. Everything that reads disc files goes through here, so the DVD
// layer, the FaceLib seed and the layout patches do not each have to know which one it is.
namespace DiscFiles {

// True when the configured game is an image file rather than an extracted folder.
bool ImageMode();

// Opens the image on first use (idempotent). False with `error` set when it can't be opened.
bool Open(std::string& error);

// The image's files ("Stage/01_01.arc", "sys/main.dol"), its ID and revision. Empty before Open().
const std::vector<WiiDisc::Disc::File>& Files();
const std::string& Id();
int Revision();

// `length` bytes of the decrypted partition at `offset`, the space the FST's own file offsets live
// in. False when the image can't produce them.
bool ReadAt(uint64_t offset, uint32_t length, std::vector<uint8_t>& out);

// A whole file of the image, by disc path. False when the disc has no such file.
bool ReadWhole(std::string_view path, std::vector<uint8_t>& out);

// Size of a file of the image, or UINT64_MAX when it has none.
uint64_t Size(std::string_view path);

} // namespace DiscFiles
