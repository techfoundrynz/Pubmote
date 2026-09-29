import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from slint_codegen import split_resource_declarations, prepare_font_resources


class ResourceHeaderTests(unittest.TestCase):
    def test_private_font_changes_do_not_change_public_api(self):
        api = '#pragma once\nclass AppWindow {};\n\n'
        first = 'extern const uint8_t slint_embedded_resource_12_glyph[18];\n'
        second = 'extern const uint8_t slint_embedded_resource_12_glyph[54];\n'
        public_a, private_a = split_resource_declarations(api + first)
        public_b, private_b = split_resource_declarations(api + second)
        self.assertEqual(public_a, public_b)
        self.assertIn(first, private_a)
        self.assertIn(second, private_b)

    def test_preserves_all_declaration_types_and_bounds(self):
        declarations = (
            'extern const uint8_t slint_embedded_resource_1_data[42];\n'
            'extern const slint::cbindgen_private::BitmapFont slint_embedded_resource_12;\n'
        )
        public, private = split_resource_declarations('#pragma once\n' + declarations)
        self.assertNotIn('slint_embedded_resource_', public)
        self.assertTrue(private.endswith(declarations))

    def test_rejects_public_inline_resource_references(self):
        with self.assertRaises(ValueError):
            split_resource_declarations(
                'extern const uint8_t slint_embedded_resource_1_data[42];\n'
                'inline auto image() { return slint_embedded_resource_1_data; }\n'
            )

    def test_preserves_unicode_in_public_api(self):
        public, _ = split_resource_declarations(
            '// Temperature: °C — ready\n'
            'extern const uint8_t slint_embedded_resource_1_data[42];\n'
        )
        self.assertIn('°C — ready', public)

    def test_rejects_unrecognized_generator_output(self):
        with self.assertRaises(ValueError):
            split_resource_declarations('#pragma once\nclass AppWindow {};\n')


class FontStorageTests(unittest.TestCase):
    source = (
        '#include "app-window-resources.h"\n'
        'const slint::cbindgen_private::BitmapGlyph slint_embedded_resource_2_glyphset_0[1] = '
        '{ { .data = slint::private_api::make_slice(slint_embedded_resource_2_pixels, 4) } };\n'
        'const slint::cbindgen_private::BitmapGlyphs slint_embedded_resource_2_glyphsets[1] = '
        '{ { .glyph_data = slint::private_api::make_slice(slint_embedded_resource_2_glyphset_0, 1) } };\n'
        'auto runtime() { return slint::private_api::make_slice(runtime_data, 4); }\n'
    )

    def test_flash_requires_constant_initialization_and_preserves_runtime_code(self):
        result = prepare_font_resources(self.source)
        self.assertEqual(result.count('constinit const slint::'), 2)
        self.assertIn('pubremote_font_slice(slint_embedded_resource_2_pixels, 4)', result)
        self.assertIn('slint::private_api::make_slice(runtime_data, 4)', result)
        self.assertNotIn('heap_caps_malloc', result)

    def test_unknown_font_definition_fails_instead_of_silently_using_ram(self):
        with self.assertRaisesRegex(ValueError, "Unrecognized font resource definition"):
            prepare_font_resources(self.source.replace('[1]', '[glyph_count]'))

    def test_unknown_generator_slice_fails_instead_of_silently_using_ram(self):
        with self.assertRaises(ValueError):
            prepare_font_resources(self.source.replace('2_pixels, 4', '2_pixels, count'))

    def test_no_font_definitions_is_unchanged(self):
        self.assertEqual(prepare_font_resources('void callback() {}'), 'void callback() {}')


if __name__ == '__main__':
    unittest.main()
