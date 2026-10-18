/* Full-path, case-insensitive PK3 resource access for the mod runtime.
 * Unlike WAD lump lookup, names retain their namespace and extension.
 * Nothing is extracted to a path supplied by an archive. */
#ifndef DOOM_MOD_ARCHIVE_H
#define DOOM_MOD_ARCHIVE_H
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace doommod {
class Archive {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit Archive(const std::string &path);
    ~Archive();
    Archive(const Archive &)=delete;
    Archive &operator=(const Archive &)=delete;
    bool contains(const std::string &name) const;
    std::vector<std::uint8_t> read(const std::string &name) const;
    std::string text(const std::string &name) const;
    std::vector<std::string> names() const;
    static std::string normalize(const std::string &name);
};
}
#endif
