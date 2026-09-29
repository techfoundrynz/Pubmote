"""Isolate Slint resources and keep immutable font metadata in flash."""
import re


RESOURCE_DECLARATION = re.compile(
    r"^extern const [^\n;]*\bslint_embedded_resource_\w+[^\n;]*;[ \t]*\n?", re.MULTILINE
)


def split_resource_declarations(header):
    """Preserve declarations verbatim; reject generator formats we don't understand.

    Font resource numbering can vary between invocations of the pinned compiler.
    Only generated implementation files need those declarations. Leaving them in
    the public header invalidates every handwritten UI consumer unnecessarily.
    """
    declarations = RESOURCE_DECLARATION.findall(header)
    if not declarations:
        raise ValueError("Slint header has no recognized resource declarations")
    public = RESOURCE_DECLARATION.sub("", header)
    if "slint_embedded_resource_" in public:
        raise ValueError("Slint public header references resources outside standalone declarations")
    public = re.sub(r"\n{3,}", "\n\n", public).rstrip() + "\n"
    private = '#pragma once\n#include "app-window.h"\n\n' + "".join(declarations)
    return public, private


# Restrict rewriting to generated font resources, never runtime UI expressions.
FONT_RESOURCE = re.compile(
    r"^const slint::cbindgen_private::(BitmapGlyph|BitmapGlyphs|BitmapFont) "
    r"(slint_embedded_resource_\w+)(\[\d+\])? = ([\s\S]*?);", re.MULTILINE
)
RESOURCE_SLICE = re.compile(
    r"slint::private_api::make_slice\((slint_embedded_resource_\w+)\s*,\s*(\d+)\)"
)

FONT_SLICE_HELPER = '''
// Resource arrays always have a non-null static address, including empty arrays.
// Keep their slices constant-initialized instead of consuming internal DRAM.
template<typename T>
constexpr slint::cbindgen_private::Slice<T> pubremote_font_slice(const T *ptr, size_t len) {
    return {const_cast<T *>(ptr), len};
}
'''

def prepare_font_resources(content):
    """Require constant initialization for generated immutable font resources.

    constinit makes unsupported initializers fail compilation instead of silently
    consuming internal RAM. Only slices of static resource arrays are rewritten.
    """
    def resource(match):
        kind, name, bounds, body = match.groups()
        body = RESOURCE_SLICE.sub(
            lambda item: f"pubremote_font_slice({item[1]}, {item[2]})", body
        )
        if "slint::private_api::make_slice" in body:
            raise ValueError("Unrecognized font slice initializer: " + name)
        return f"constinit const slint::cbindgen_private::{kind} {name}{bounds or ''} = {body};"

    result, count = FONT_RESOURCE.subn(resource, content)
    if re.search(r"^const slint::cbindgen_private::(?:BitmapGlyphs?|BitmapFont)\b", result, re.MULTILINE):
        raise ValueError("Unrecognized font resource definition")
    if not count:
        return content
    include = '#include "app-window-resources.h"'
    if result.count(include) != 1:
        raise ValueError("Missing private resource header")
    return result.replace(include, include + "\n" + FONT_SLICE_HELPER, 1)
