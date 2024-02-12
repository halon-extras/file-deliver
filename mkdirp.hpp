#ifndef MKDIRP_HPP
#define MKDIRP_HPP

#include <string>

bool mkdirp(const std::string& path, mode_t mode = 0777);

#endif
