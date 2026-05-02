#include <base/assert.h>
#include <base/config.h>
#include <render/render.h>

namespace gui::render {

    auto load_image_texture_from_file(Context context, StrRef path, Texture& out_texture)
        -> Result {
        BASE_UNUSED(context);
        BASE_UNUSED(path);
        BASE_UNUSED(out_texture);
        return Result::UNSUPPORTED_PLATFORM;
    }

    auto load_image_texture_from_memory(
        Context context, void const* bytes, size_t byte_count, Texture& out_texture
    ) -> Result {
        BASE_UNUSED(context);
        BASE_UNUSED(bytes);
        BASE_UNUSED(byte_count);
        BASE_UNUSED(out_texture);
        return Result::UNSUPPORTED_PLATFORM;
    }

} // namespace gui::render
