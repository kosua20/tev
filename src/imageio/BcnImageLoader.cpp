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

		const nanogui::Vector2i size{sliceInfo.width, sliceInfo.height};
		const unsigned int channelCount =  texInfo.format == DDSKTX_FORMAT_BC6H ? 3u : texInfo.format == DDSKTX_FORMAT_BC5 ? 2u : texInfo.format == DDSKTX_FORMAT_BC4 ? 1u : 4u;
		const unsigned int bytesPerChannel = texInfo.format == DDSKTX_FORMAT_BC6H ? 4u : 1u;
		resultData.channels = makeNChannels(channelCount, size);
		const unsigned int dstPitch = channelCount * bytesPerChannel * sliceInfo.width;
		// Allocate destination.
		std::vector<unsigned char> dstData(sliceInfo.width * sliceInfo.height * channelCount * bytesPerChannel);

		// Uncompress.
		unsigned char* src = (unsigned char*)sliceInfo.buff;
		unsigned char* dst = dstData.data();

		for(int i = 0; i < sliceInfo.height; i += 4){

			for(int j = 0; j < sliceInfo.width; j += 4){
				unsigned char* dstLine = dst + (i * sliceInfo.width + j) * bytesPerChannel * channelCount;
				switch (texInfo.format) {
					case DDSKTX_FORMAT_BC1:
						bcdec_bc1(src, dstLine, dstPitch);
						src += BCDEC_BC1_BLOCK_SIZE;
						break;
					case DDSKTX_FORMAT_BC2:
						bcdec_bc2(src, dstLine, dstPitch);
						src += BCDEC_BC2_BLOCK_SIZE;
						break;
					case DDSKTX_FORMAT_BC3:
						bcdec_bc3(src, dstLine, dstPitch);
						src += BCDEC_BC3_BLOCK_SIZE;
						break;
					case DDSKTX_FORMAT_BC4:
						bcdec_bc4(src, dstLine, dstPitch);
						src += BCDEC_BC4_BLOCK_SIZE;
						break;
					case DDSKTX_FORMAT_BC5:
						bcdec_bc5(src, dstLine, dstPitch);
						src += BCDEC_BC5_BLOCK_SIZE;
						break;
					case DDSKTX_FORMAT_BC6H:
						bcdec_bc6h_float(src, dstLine, dstPitch / 4u, true);
						// assume signed for now, need to check FourCC.
						src += BCDEC_BC6H_BLOCK_SIZE;
						break;
					case DDSKTX_FORMAT_BC7:
						bcdec_bc7(src, dstLine, dstPitch);
						src += BCDEC_BC7_BLOCK_SIZE;
						break;
					default:
						break;
				}
			}

		}

		// Transfer to channels
		for(int i = 0; i < sliceInfo.width * sliceInfo.height; ++i){
			for(int c = 0; c < (int)channelCount; ++c){
				int idx = (i * channelCount + c) * bytesPerChannel;

				if(texInfo.format == DDSKTX_FORMAT_BC6H){
					resultData.channels[c].at(i) = *((float*)&dstData[idx]);
				} else {
					resultData.channels[c].at(i) = ((float)dstData[idx])/255.0f;
				}

				// Gamma conversion (even is sRGB flag not present?)
				if(c != 3){
					resultData.channels[c].at(i) = toLinear(resultData.channels[c].at(i));
				}
			}
		}

		resultData.hasPremultipliedAlpha = false;
		resultData.partName = "Mip " + std::to_string(mipIdx);
	}

	co_return result;
}

TEV_NAMESPACE_END
