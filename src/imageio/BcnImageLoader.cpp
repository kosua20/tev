// This file was developed by Simon Rodriguez.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include <tev/imageio/BcnImageLoader.h>
#include <tev/ThreadPool.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>

#define BCDEC_IMPLEMENTATION
#include <bcdec.h>

#include <unordered_map>

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

	struct FormatInfos {
		int channelCount;
		int bytesPerChannel;
		int srcStride;
	};

	const std::unordered_map<ddsktx_format, FormatInfos> formatInfos = {
		{ DDSKTX_FORMAT_BC1,	{ 4, 1, BCDEC_BC1_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BC2,	{ 4, 1, BCDEC_BC2_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BC3,	{ 4, 1, BCDEC_BC3_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BC4,	{ 1, 1, BCDEC_BC4_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BC5,	{ 2, 1, BCDEC_BC5_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BC6H,	{ 3, 4, BCDEC_BC6H_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BC7,	{ 4, 1, BCDEC_BC7_BLOCK_SIZE }},
		{ DDSKTX_FORMAT_BGRA8,	{ 4, 1, 4 }},
		{ DDSKTX_FORMAT_RGBA8,	{ 4, 1, 4 }},
		{ DDSKTX_FORMAT_RGB8,	{ 3, 1, 3 }},
		{ DDSKTX_FORMAT_RG8,	{ 2, 1, 2 }},
		{ DDSKTX_FORMAT_RGBA16F,{ 4, 4, 4 * 2 }},
		{ DDSKTX_FORMAT_RG16F,	{ 2, 4, 2 * 2 }},
		{ DDSKTX_FORMAT_R16F,	{ 1, 4, 1 * 2 }},
		{ DDSKTX_FORMAT_R8,		{ 1, 1, 1 }},
		{ DDSKTX_FORMAT_A8,		{ 1, 1, 1 }},
		{ DDSKTX_FORMAT_R32F,	{ 1, 4, 1 * 4 }},
	};
	/* Missing:
	 DDSKTX_FORMAT_RGBA8S,
	 DDSKTX_FORMAT_RG16,
	 DDSKTX_FORMAT_R16,
	 DDSKTX_FORMAT_RG16S,
	 DDSKTX_FORMAT_RGBA16,
	 DDSKTX_FORMAT_RG8S
	 */

	if(formatInfos.count(texInfo.format) == 0){
		throw invalid_argument{ fmt::format("DDS image does not use supported format ({}).",
											ddsktx_format_str(texInfo.format)) };
	}

	if((texInfo.flags & DDSKTX_TEXTURE_FLAG_VOLUME) != 0u){
		tlog::warning("DDS 3D image not supported.");
	}
	if(texInfo.depth != 1u){
		tlog::warning("DDS 3D image not supported.");
	}
	// TODO: support 3D images (which layout?)

	if((texInfo.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP) != 0u){
		tlog::warning("DDS cube image not supported.");
	}
	// TODO: support cubemap images (which layout?)

	if(texInfo.num_layers != 1u){
		tlog::warning("DDS array image not supported.");
	}
	// TODO: support array images (which layout?)

	const bool isSrgb = (texInfo.flags & DDSKTX_TEXTURE_FLAG_SRGB) != 0u;
	const bool hasAlpha = (texInfo.flags & DDSKTX_TEXTURE_FLAG_ALPHA) != 0u;

	// TODO: How should we lay out the slices/mips/faces.
	// For now, one part per mip, ignore the channel selector (which should be used with matchesFuzzy(name, selector))
	// Instead maybe slice/face as part, mip as channel group.

	// TODO: some helper to handle decompression separately from face/mips parsing.

	// TODO: taskify.

	const FormatInfos& format = formatInfos.at(texInfo.format);
	const int stepSize = ddsktx_format_compressed(texInfo.format) ? 4 : 1;

	for(int mipIdx = 0; mipIdx < texInfo.num_mips; ++mipIdx){
		ddsktx_sub_data sliceInfo;
		ddsktx_get_sub(&texInfo, &sliceInfo, data.data(), dataSize, 0, 0, mipIdx);

		if(sliceInfo.width == 0 || sliceInfo.height == 0){
			tlog::warning() << "DDS mip " << mipIdx << " has zero pixels.";
			continue;
		}

		result.emplace_back();
		ImageData& resultData = result.back();

		const nanogui::Vector2i size{std::max(stepSize, sliceInfo.width), std::max(stepSize, sliceInfo.height)};
		
		resultData.channels = makeNChannels(format.channelCount, size);
		const unsigned int dstPitch = format.channelCount * format.bytesPerChannel * size.x();
		// Allocate destination.
		std::vector<unsigned char> dstData(size.y() * dstPitch);

		// Uncompress.
		unsigned char* src = (unsigned char*)sliceInfo.buff;
		unsigned char* dst = dstData.data();

		for(int i = 0; i < sliceInfo.height; i += stepSize){

			for(int j = 0; j < sliceInfo.width; j += stepSize){
				unsigned char* dstLine = dst + (i * size.x() + j) * format.channelCount * format.bytesPerChannel;
				switch (texInfo.format) {
					case DDSKTX_FORMAT_BC1:
						bcdec_bc1(src, dstLine, dstPitch);
						break;
					case DDSKTX_FORMAT_BC2:
						bcdec_bc2(src, dstLine, dstPitch);
						break;
					case DDSKTX_FORMAT_BC3:
						bcdec_bc3(src, dstLine, dstPitch);
						break;
					case DDSKTX_FORMAT_BC4:
						bcdec_bc4(src, dstLine, dstPitch);
						break;
					case DDSKTX_FORMAT_BC5:
						bcdec_bc5(src, dstLine, dstPitch);
						break;
					case DDSKTX_FORMAT_BC6H:
						bcdec_bc6h_float(src, dstLine, dstPitch / 4u, true);
						// assume signed for now, need to check FourCC.
						break;
					case DDSKTX_FORMAT_BC7:
						bcdec_bc7(src, dstLine, dstPitch);
						break;
					case DDSKTX_FORMAT_BGRA8:
						dst[0] = src[2];
						dst[1] = src[1];
						dst[2] = src[0];
						dst[3] = src[3];
						break;
					case DDSKTX_FORMAT_RGBA8:
						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
						dst[3] = src[3];
						break;
					case DDSKTX_FORMAT_RGB8:
						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
						break;
					case DDSKTX_FORMAT_RG8:
						dst[0] = src[0];
						dst[1] = src[1];
						break;
					case DDSKTX_FORMAT_R8:
					case DDSKTX_FORMAT_A8:
						dst[0] = src[0];
						break;
					case DDSKTX_FORMAT_R32F:
						((float*)dst)[0] = ((float*)src)[0];
						break;
					case DDSKTX_FORMAT_R16F:
						{
							unsigned short v = (src[0] << 16) | src[1];
							((float*)dst)[0] = bcdec__half_to_float_quick(v);
						}
						break;
					case DDSKTX_FORMAT_RG16F:
						for(int c = 0; c < 2; ++c){
							unsigned short v = (src[2 * c] << 16) | src[2 * c + 1];
							((float*)dst)[c] = bcdec__half_to_float_quick(v);
						}
						break;
					case DDSKTX_FORMAT_RGBA16F:
						for(int c = 0; c < 4; ++c){
							unsigned short v = (src[2 * c] << 16) | src[2 * c + 1];
							((float*)dst)[c] = bcdec__half_to_float_quick(v);
						}
						break;
					default:
						break;
				}
				src += format.srcStride;
			}
		}

		// Transfer to channels
		for(int i = 0; i < size.x() * size.y(); ++i){
			for(int c = 0; c < (int)format.channelCount; ++c){
				int idx = (i * format.channelCount + c) * format.bytesPerChannel;

				if(format.bytesPerChannel == 4u){
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
