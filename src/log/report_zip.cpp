// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log/report_zip.h"

#include <miniz.h>

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace kalve137::log {

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

void writeZip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries)
{
    std::vector<std::string> data;
    data.reserve(entries.size());
    for (const ZipEntry& entry : entries) {
        data.push_back(entry.source.empty() ? entry.contents : readFile(entry.source));
    }

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        throw std::runtime_error("Cannot start the zip");
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!mz_zip_writer_add_mem(&zip, entries[i].name.c_str(), data[i].data(), data[i].size(), MZ_DEFAULT_LEVEL)) {
            mz_zip_writer_end(&zip);
            throw std::runtime_error("Cannot add " + entries[i].name + " to the zip");
        }
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        mz_zip_writer_end(&zip);
        throw std::runtime_error("Cannot finish the zip");
    }
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    zip.m_pFree(zip.m_pAlloc_opaque, buffer);
    mz_zip_writer_end(&zip);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

std::vector<std::string> zipEntries(const std::filesystem::path& path)
{
    const std::string data = readFile(path);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)) {
        throw std::runtime_error("Not a zip: " + path.string());
    }
    std::vector<std::string> names;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        names.emplace_back(name);
    }
    mz_zip_reader_end(&zip);
    return names;
}

} // namespace kalve137::log
