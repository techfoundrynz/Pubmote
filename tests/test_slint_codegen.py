import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from slint_codegen import split_resource_declarations


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


if __name__ == '__main__':
    unittest.main()
