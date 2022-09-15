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

	// TODO: Missing: DDSKTX_FORMAT_RGBA8S,
	// TODO: Missing: DDSKTX_FORMAT_RG16,
	// TODO: Missing: DDSKTX_FORMAT_R16,
	// TODO: Missing: DDSKTX_FORMAT_RG16S,
	// TODO: Missing: DDSKTX_FORMAT_RGBA16,
	// TODO: Missing: DDSKTX_FORMAT_RGB10A2,
	// TODO: Missing: DDSKTX_FORMAT_RG11B10F,
	// TODO: Missing: DDSKTX_FORMAT_RG8S


	if(formatInfos.count(texInfo.format) == 0){
		throw invalid_argument{ fmt::format("DDS image does not use supported format ({}).",
											ddsktx_format_str(texInfo.format)) };
	}

	// TODO: if !hasAlpha, clear the alpha channel?
	// TODO: if isSrgb, which conversion to apply/skip?
	const bool isSrgb = (texInfo.flags & DDSKTX_TEXTURE_FLAG_SRGB) != 0u;
	const bool hasAlpha = (texInfo.flags & DDSKTX_TEXTURE_FLAG_ALPHA) != 0u;
	const bool isCube = (texInfo.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP) != 0u;
	const bool is3D = ((texInfo.flags & DDSKTX_TEXTURE_FLAG_VOLUME) != 0u) || (texInfo.depth != 1u);
	const bool isArray = texInfo.num_layers != 1u;

	// TODO: How should we lay out the slices/mips/faces.
	// For now, one part per mip (because different sizes), ignore the channel selector (which should be used with matchesFuzzy(name, selector)), and layer/face as channel layers.
	// For cubemaps, maybe flatten in a cross?
	// Display string for layers should be compacted.

	// TODO: taskify.

	const FormatInfos& format = formatInfos.at(texInfo.format);
	const int stepSize = ddsktx_format_compressed(texInfo.format) ? 4 : 1;
	const int effectiveDepth = isCube ? 6 : texInfo.depth;

	for(int mipIdx = 0; mipIdx < texInfo.num_mips; ++mipIdx){

		// All channels in an image data should have the same size.
		result.emplace_back();
		ImageData& resultData = result.back();
		
		for(int layIdx = 0; layIdx < texInfo.num_layers; ++layIdx){

			for(int faceIdx = 0; faceIdx < effectiveDepth; ++faceIdx){

				ddsktx_sub_data sliceInfo;
				ddsktx_get_sub(&texInfo, &sliceInfo, data.data(), dataSize, layIdx, faceIdx, mipIdx);

				if(sliceInfo.width == 0 || sliceInfo.height == 0){
					tlog::warning() << "DDS mip " << mipIdx << " has zero pixels.";
					continue;
				}

				const nanogui::Vector2i size{std::max(stepSize, sliceInfo.width), std::max(stepSize, sliceInfo.height)};
				string baseName;

				if(isArray){
					baseName += "Layer " + std::to_string(layIdx);
				}

				if(isCube){
					const std::vector<string> faceNames = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
					baseName += "Face " + faceNames[faceIdx];
				}
				if(is3D){
					baseName += "Slice " + std::to_string(faceIdx);
				}

				if(!baseName.empty()){
					resultData.layers.push_back(baseName);
				}
				
				const int firstChannel = resultData.channels.size();
				if (format.channelCount > 1) {
					const vector<string> channelNames = {"R", "G", "B", "A"};
					for (int c = 0; c < format.channelCount; ++c) {
						string name = c < (int)channelNames.size() ? channelNames[c] : to_string(c);
						resultData.channels.emplace_back( baseName + name, size);
					}
				} else {
					resultData.channels.emplace_back( baseName + "L", size);
				}
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
							resultData.channels[firstChannel + c].at(i) = *((float*)&dstData[idx]);
						} else {
							resultData.channels[firstChannel + c].at(i) = ((float)dstData[idx])/255.0f;
						}

						// Gamma conversion (even is sRGB flag not present?)
						if(c != 3){
							resultData.channels[firstChannel + c].at(i) = toLinear(resultData.channels[firstChannel + c].at(i));
						}
					}
				}
			}
		}

		resultData.hasPremultipliedAlpha = false;
		resultData.partName = "Mip " + std::to_string(mipIdx);
	}

	co_return result;
}

TEV_NAMESPACE_END
