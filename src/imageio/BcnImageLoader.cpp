// This file was developed by Simon Rodriguez.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include <tev/imageio/BcnImageLoader.h>
#include <tev/ThreadPool.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>

#define BCDEC_IMPLEMENTATION
#include <bcdec.h>

using namespace nanogui;
using namespace std;

TEV_NAMESPACE_BEGIN

bool BcnImageLoader::canLoadFile(istream& iStream) const {
    char b[4];
    iStream.read(b, sizeof(b));

	bool result = !!iStream && iStream.gcount() == sizeof(b) && (b[0] == 'D' && b[1] == 'D' && b[2] == 'S' && b[3] == ' ');
	// TODO: other (0xAB, 'K', 'T', 'X', DXT direct files)...

	iStream.clear();
	iStream.seekg(0);

    return result;
}

Task<vector<ImageData>> BcnImageLoader::load(istream& iStream, const fs::path&, const string& channelSelector, int priority) const {

	vector<ImageData> result;


	co_return result;
}

TEV_NAMESPACE_END
