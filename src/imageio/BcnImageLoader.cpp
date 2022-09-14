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

	// Load everything at once.
	iStream.seekg(0, iStream.end);
	size_t dataSize = iStream.tellg();
	iStream.seekg(0, iStream.beg);
	vector<char> data(dataSize);
	iStream.read(data.data(), dataSize);

	ddsktx_texture_info texInfo = {};
	if(!ddsktx_parse(&texInfo, data.data(), dataSize)){
		throw invalid_argument{"Unable to parse DDS image header."};
	}

	if(texInfo.num_mips <= 0) {
		throw invalid_argument{"DDS image does not contain any mip."};
	}

	if(texInfo.format != DDSKTX_FORMAT_BC1 &&
	   texInfo.format != DDSKTX_FORMAT_BC2 &&
	   texInfo.format != DDSKTX_FORMAT_BC3 &&
	   texInfo.format != DDSKTX_FORMAT_BC4 &&
	   texInfo.format != DDSKTX_FORMAT_BC5 &&
	   texInfo.format != DDSKTX_FORMAT_BC6H &&
	   texInfo.format != DDSKTX_FORMAT_BC7
	   ){
		throw invalid_argument{"DDS image does not use BCn format."};
	}
	// TODO: support non compressed formats.

	if((texInfo.flags & DDSKTX_TEXTURE_FLAG_VOLUME) != 0u){
		throw invalid_argument{"DDS 3D image not supported."};
	}
	if(texInfo.depth != 1u){
		throw invalid_argument{"DDS 3D image not supported."};
	}
	// TODO: support 3D images (which layout?)

	if((texInfo.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP) != 0u){
		throw invalid_argument{"DDS cube image not supported."};
	}
	// TODO: support cubemap images (which layout?)

	if(texInfo.num_layers != 1u){
		throw invalid_argument{"DDS array image not supported."};
	}
	// TODO: support array images (which layout?)

	const bool isSrgb = (texInfo.flags & DDSKTX_TEXTURE_FLAG_SRGB) != 0u;
	const bool hasAlpha = (texInfo.flags & DDSKTX_TEXTURE_FLAG_ALPHA) != 0u;

	// TODO: How should we lay out the slices/mips/faces.
	// For now, one part per mip, ignore the channel selector (which should be used with matchesFuzzy(name, selector))
	// Instead maybe slice/face as part, mip as channel group.

	// TODO: some helper to handle decompression separately from face/mips parsing.

	// TODO: taskify.

	for(int mipIdx = 0; mipIdx < texInfo.num_mips; ++mipIdx){
		ddsktx_sub_data sliceInfo;
		ddsktx_get_sub(&texInfo, &sliceInfo, data.data(), dataSize, 0, 0, mipIdx);

		if(sliceInfo.width == 0 || sliceInfo.height == 0){
			tlog::warning() << "DDS mip " << mipIdx << " has zero pixels.";
			continue;
		}

		result.emplace_back();
		ImageData& resultData = result.back();
		resultData.hasPremultipliedAlpha = false;
		resultData.partName = "Mip " + std::to_string(mipIdx);
	}

	co_return result;
}

TEV_NAMESPACE_END
