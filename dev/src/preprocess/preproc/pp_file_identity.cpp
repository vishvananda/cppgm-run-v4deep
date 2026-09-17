#include "preprocess/preproc/pp_file_identity.h"

#include <sys/stat.h>

namespace cppgm
{
namespace preprocess
{

bool GetPreprocessorFileId(const std::string& path, PreprocessorFileId& fileid)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return false;
	fileid = std::make_pair(static_cast<unsigned long long>(info.st_dev),
	                        static_cast<unsigned long long>(info.st_ino));
	return true;
}

} // namespace preprocess
} // namespace cppgm