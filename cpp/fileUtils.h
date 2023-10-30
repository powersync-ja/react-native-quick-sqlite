#include <string>

bool folder_exists(const std::string &foldername);

/**
 * Portable wrapper for mkdir. Internally used by mkdir()
 * @param[in] path the full path of the directory to create.
 * @return zero on success, otherwise -1.
 */
int _mkdir(const char *path);

/**
 * Recursive, portable wrapper for mkdir.
 * @param[in] path the full path of the directory to create.
 * @return zero on success, otherwise -1.
 */
int mkdir(const char *path);

bool file_exists(const std::string &path);

std::string get_db_path(std::string const dbName, std::string const docPath);