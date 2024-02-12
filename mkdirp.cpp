#include "mkdirp.hpp"

#include <string>
#include <vector>
#include <sys/stat.h>

/*
 * This function will create all folders in the whole path (like mkdir -p)..
 * Mode is unmasked
 *
 * eg.
 *    mkdirp("/foo/bar/baz", 0777);
 */
bool mkdirp(const std::string& path, mode_t mode)
{
	std::vector<std::string> path_list;
	size_t tmp = 0;
	size_t pos = 0;
	while ((tmp = path.substr(pos).find_first_of('/')) != std::string::npos) {
		pos += tmp + 1;
		path_list.push_back(path.substr(0, pos));
	}

	if (path.substr(path.size() - 1, 1) != "/")
		path_list.push_back(path + "/");

	// this algorithm is optimistic, it tries the path up the tree

	auto p = path_list.rbegin();
	for (; p != path_list.rend(); ++p)
	{
		struct stat sb;
		if (stat(p->c_str(), &sb) == 0)
		{
			if (p == path_list.rbegin())
				return true;
			break;
		}
	}

	while (p != path_list.rbegin())
	{
		--p;
		if (mkdir(p->c_str(), mode) != 0)
			if (errno != EEXIST)
				return false;
	}

	return true;
}
