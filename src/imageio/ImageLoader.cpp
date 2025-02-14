// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include <tev/imageio/ClipboardImageLoader.h>
#include <tev/imageio/EmptyImageLoader.h>
#include <tev/imageio/ExrImageLoader.h>
#include <tev/imageio/ImageLoader.h>
#include <tev/imageio/PfmImageLoader.h>
#include <tev/imageio/QoiImageLoader.h>
#include <tev/imageio/StbiImageLoader.h>
#include <tev/imageio/BcnImageLoader.h>
#ifdef _WIN32
#   include <tev/imageio/DdsImageLoader.h>
#endif

using namespace nanogui;
using namespace std;

TEV_NAMESPACE_BEGIN

const vector<unique_ptr<ImageLoader>>& ImageLoader::getLoaders() {
    auto makeLoaders = [] {
        vector<unique_ptr<ImageLoader>> imageLoaders;
        imageLoaders.emplace_back(new ExrImageLoader());
        imageLoaders.emplace_back(new PfmImageLoader());
        imageLoaders.emplace_back(new ClipboardImageLoader());
        imageLoaders.emplace_back(new EmptyImageLoader());
		imageLoaders.emplace_back(new BcnImageLoader());
#ifdef _WIN32
        imageLoaders.emplace_back(new DdsImageLoader());
#endif
        imageLoaders.emplace_back(new QoiImageLoader());
        imageLoaders.emplace_back(new StbiImageLoader());
        return imageLoaders;
    };

    static const vector imageLoaders = makeLoaders();
    return imageLoaders;
}

vector<Channel> ImageLoader::makeNChannels(int numChannels, const Vector2i& size) {
    vector<Channel> channels;
    if (numChannels > 1) {
        const vector<string> channelNames = {"R", "G", "B", "A"};
        for (int c = 0; c < numChannels; ++c) {
            string name = c < (int)channelNames.size() ? channelNames[c] : to_string(c);
            channels.emplace_back(name, size);
        }
    } else {
        channels.emplace_back("L", size);
    }

    return channels;
}

TEV_NAMESPACE_END
