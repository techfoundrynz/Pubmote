"""Keep Slint's private resource declarations out of its public UI header."""
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
