#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/id3v2tag.h>

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 30
#endif

#include <fuse3/fuse.h>

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

namespace fs = std::filesystem;
using namespace std::literals;

static constexpr short min_args_num = 3;
static constexpr short source_dir_index = 1;
static constexpr short mount_point_index = 2;
static constexpr int dir_rights = 0755;
static constexpr int dir_number_links = 2;
static constexpr std::string_view artists_dir_name = "/Artists"sv;
static constexpr std::string_view genres_dir_name = "/Genres"sv;
static constexpr std::string_view years_dir_name = "/Years"sv;
static constexpr std::string_view artists_subdir_name = "/Artists/"sv;
static constexpr std::string_view genres_subdir_name = "/Genres/"sv;
static constexpr std::string_view years_subdir_name = "/Years/"sv;
static constexpr size_t artists_subdir_len = artists_subdir_name.size();
static constexpr size_t genres_subdir_len = genres_subdir_name.size();
static constexpr size_t years_subdir_len = years_subdir_name.size();

static constexpr std::string_view searched_fe = ".mp3";
static constexpr std::string default_dir = "Unknown";
static constexpr char* root_dir = ".";
static constexpr char* previous_dir = "..";
static const char* const category_tags[] = {"Artists", "Genres", "Years"};

struct FileInfo {
    fs::path real_path;
    struct stat status;
};

struct TrackTags {
    std::string artist = default_dir;
    std::string genre = default_dir;
    std::string release_year = default_dir;
};

std::unordered_map<std::string, FileInfo> all_tracks;
std::unordered_map<std::string, std::unordered_set<std::string>> by_artist;
std::unordered_map<std::string, std::unordered_set<std::string>> by_genre;
std::unordered_map<std::string, std::unordered_set<std::string>> by_release_year;

TrackTags read_tags(const fs::path& path) {
    TrackTags tags;
    if (TagLib::FileRef file(path.c_str()); !file.isNull() && file.tag()) {
        auto* tag = file.tag();
        tags.artist = tag->artist().isEmpty() ? tags.artist : tag->artist().to8Bit(true);
        tags.genre = tag->genre().isEmpty() ? tags.genre : tag->genre().to8Bit(true);
        tags.release_year = tag->year() <= 0 ? tags.release_year : std::to_string(tag->year());
    }
    return tags;
}

void fill_subdirectory_info(std::string path, size_t searched_size,
                            std::unordered_map<std::string, std::unordered_set<std::string>>& catalog,
                            void* buf, fuse_fill_dir_t filler) {
    std::string searched = path.substr(searched_size);
    if (catalog.contains(searched)) {
        for (const auto& file_path: catalog[searched]) {
            std::string filename = fs::path(file_path).filename().string();
            filler(buf, filename.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
        }
    }
}

void init_filesystem(const char* source_dir) {
    for (const auto& entry: fs::recursive_directory_iterator(source_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != searched_fe) {
            continue;
        }

        TrackTags tags = read_tags(entry.path());
        by_artist[tags.artist].insert(entry.path().string());
        by_genre[tags.genre].insert(entry.path().string());
        by_release_year[tags.release_year].insert(entry.path().string());

        FileInfo track_info;
        track_info.real_path = entry.path();
        stat(entry.path().c_str(), &track_info.status);

        std::string track_name = "/" + entry.path().filename().string();
        std::string artist_path = std::string(artists_subdir_name) + tags.artist + track_name;
        std::string genre_path = std::string(genres_subdir_name) + tags.genre + track_name;
        std::string year_path = std::string(years_subdir_name) + tags.release_year + track_name;

        all_tracks[artist_path] = track_info;
        all_tracks[genre_path] = track_info;
        all_tracks[year_path] = track_info;
    }

    std::cout << "Found " << by_artist.size() << " artists, "
              << by_genre.size() << " genres, "
              << by_release_year.size() << " years" << std::endl;
}

static int fs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info*) {
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | dir_rights;
        stbuf->st_nlink = dir_number_links;
        return 0;
    }

    if (all_tracks.contains(path)) {
        auto it = all_tracks.find(path);
        *stbuf = it->second.status;
        return 0;
    }

    std::string_view str_path(path);
    if (!str_path.empty() && str_path.back() == '/') {
        str_path = str_path.substr(0, str_path.size() - 1);
    }

    if (str_path == artists_dir_name || str_path == genres_dir_name || str_path == years_dir_name ||
        str_path.starts_with(artists_subdir_name) ||
        str_path.starts_with(genres_subdir_name) ||
        str_path.starts_with(years_subdir_name)) {
        stbuf->st_mode = S_IFDIR | dir_rights;
        stbuf->st_nlink = dir_number_links;
        return 0;
    }

    return -ENOENT;
}

static int fs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                      off_t, struct fuse_file_info*, enum fuse_readdir_flags) {
    filler(buf, root_dir, nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, previous_dir, nullptr, 0, FUSE_FILL_DIR_PLUS);

    std::string_view str_path(path);

    if (str_path == "/") {
        for (auto& category : category_tags) {
            filler(buf, category, nullptr, 0, FUSE_FILL_DIR_PLUS);
        }
    } else if (str_path == artists_dir_name) {
        for (const auto& [artist, _]: by_artist) {
            filler(buf, artist.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
        }
    } else if (str_path == genres_dir_name) {
        for (const auto& [genre, _]: by_genre) {
            filler(buf, genre.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
        }
    } else if (str_path == years_dir_name) {
        for (const auto& [year, _]: by_release_year) {
            filler(buf, year.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
        }
    } else if (str_path.starts_with(artists_subdir_name)) {
        fill_subdirectory_info(std::string(str_path), artists_subdir_len, by_artist, buf, filler);
    } else if (str_path.starts_with(genres_subdir_name)) {
        fill_subdirectory_info(std::string(str_path), genres_subdir_len, by_genre, buf, filler);
    } else if (str_path.starts_with(years_subdir_name)) {
        fill_subdirectory_info(std::string(str_path), years_subdir_len, by_release_year, buf, filler);
    }

    return 0;
}

static int fs_open(const char* path, struct fuse_file_info* fi) {
    (void) fi;
    if (!all_tracks.contains(path)) {
        return -ENOENT;
    }
    return 0;
}

static int fs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;
    if (!all_tracks.contains(path)) {
        return -ENOENT;
    }

    auto it = all_tracks.find(path);
    std::ifstream file(it->second.real_path.c_str(), std::ios::binary);
    if (!file) {
        return -EIO;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (offset >= file_size) {
        return 0;
    }
    size = std::min(file_size - offset, size);

    file.seekg(offset);
    file.read(buf, size);
    if (!file && !file.eof()) {
        return -EIO;
    }

    return static_cast<int>(file.gcount());
}

static const struct fuse_operations fs_operations = {
        .getattr = fs_getattr,
        .open = fs_open,
        .read = fs_read,
        .readdir = fs_readdir,
};

bool is_valid_directory(const std::string& path) {
    try {
        return fs::is_directory(path);
    } catch (...) {
        return false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < min_args_num) {
        std::cerr << "Usage: " << argv[0] << " <source dir> <mount point>\n";
        return EXIT_FAILURE;
    }

    if (!is_valid_directory(argv[source_dir_index])) {
        std::cerr << "Error: Source directory is invalid or doesn't exist\n";
        return EXIT_FAILURE;
    }

    if (!is_valid_directory(argv[mount_point_index])) {
        std::cerr << "Error: Mount point is invalid or doesn't exist\n";
        return EXIT_FAILURE;
    }

    init_filesystem(argv[source_dir_index]);

    char* fuse_argv[] = {argv[0], argv[mount_point_index], nullptr};
    int fuse_argc = 2;

    std::cout << "Mounting MP3 organizer at: " << argv[mount_point_index] << std::endl;
    return fuse_main(fuse_argc, fuse_argv, &fs_operations, nullptr);
}