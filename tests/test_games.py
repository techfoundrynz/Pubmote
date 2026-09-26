"""Run the actual installable Lua packages with a deterministic host API."""
import math
from pathlib import Path
import unittest
import tempfile


ROOT = Path(__file__).resolve().parents[1]


import sys
sys.path.insert(0, str(ROOT / 'scripts'))
from play_game import GameHost
from install_game import validate_package


class Game(GameHost):
    def __init__(self, name):
        self.saved = []
        super().__init__(ROOT / 'fs' / 'games' / f'{name}.lua', 123)

    def save_score(self, value):
        self.saved.append(value)
        super().save_score(value)

    @property
    def state(self):
        return self.call('inspect')

    def event(self, kind, x=0, y=0):
        self.call('event', kind, x, y)

    def update(self, dt=.033, x=0, y=0, joystick=False):
        self.call('update', dt, x, y, joystick)


class GamesTest(unittest.TestCase):
    def test_shipping_packages_have_valid_metadata_and_size(self):
        for path in (ROOT / 'fs' / 'games').glob('*.lua'):
            metadata, data = validate_package(path)
            self.assertEqual(metadata['id'], path.stem)
            self.assertGreater(len(data), 0)

    def test_packages_run_and_render_with_bounded_memory(self):
        for name in ('flappy', 'tetris', 'whack'):
            with self.subTest(name=name):
                game = Game(name)
                self.assertEqual(game.metadata['api'], 1)
                self.assertEqual(game.metadata['id'], name)
                game.draw()
                game.event(0)
                for i in range(1500):
                    if i % 7 == 0:
                        game.event(i % 5)
                    game.update(y=math.sin(i / 30), joystick=True)
                    game.draw()
                self.assertLess(game.lua.get_memory_used(), 256 * 1024)

    def test_flappy_ground_collision_saves_score_and_can_restart(self):
        game = Game('flappy')
        game.event(0)
        for _ in range(100):
            game.update()
        self.assertEqual(game.state.state, 2)
        self.assertEqual(game.saved, [])  # No NVS write unless the score improves.
        game.event(0)
        self.assertEqual(game.state.state, 1)
        game.event(0)
        self.assertEqual(game.state.v, -76)

    def test_flappy_pass_and_difficulty(self):
        game = Game('flappy'); game.event(0)
        game.state.pillars[1].x = 16
        game.state.pillars[1].y = game.state.y
        game.update(.001)
        self.assertEqual(game.state.score, 1)
        self.assertAlmostEqual(game.state.gap, 25.9)
        self.assertAlmostEqual(game.state.speed, 32.3)

    def test_tetris_clear_scoring_and_collapse(self):
        game = Game('tetris'); game.event(0)
        s = game.state
        # Vertical I fills the final column in the bottom four rows.
        for y in range(16, 20):
            for x in range(9):
                s.board[y * 10 + x + 1] = 2
        s.piece, s.rot, s.x, s.y = 1, 1, 7, 16
        game.event(5, 50, 40); game.event(7, 50, 60); game.event(6, 50, 60)
        self.assertEqual(s.lines, 4)
        self.assertEqual(s.score, 800)
        self.assertEqual(s.flash, 130)
        game.update(.1); game.update(.1)
        self.assertTrue(all(s.board[i] == 0 for i in range(1, 201)))

    def test_tetris_hold_once_per_piece(self):
        game = Game('tetris'); game.event(0)
        first = game.state.piece
        game.event(5, 50, 50); game.event(7, 50, 30); game.event(6, 50, 30)
        self.assertEqual(game.state.hold, first)
        current = game.state.piece
        game.event(5, 50, 50); game.event(7, 50, 30); game.event(6, 50, 30)
        self.assertEqual(game.state.piece, current)
        self.assertEqual(game.state.hold, first)

    def test_whack_golden_combo_croissant_and_wine(self):
        game = Game('whack'); game.event(0)
        s = game.state
        def hit(kind):
            h = s.holes[s.target]
            h.phase, h.kind, h.pop = 2, kind, 1
            game.event(0)
        for _ in range(5): hit(0)
        self.assertEqual(s.score, 6)  # Fifth hit starts x2.
        hit(1)
        self.assertEqual(s.score, 16)
        hit(2)
        self.assertEqual(s.lives, 2)
        self.assertEqual(s.combo, 0)
        hit(4)
        self.assertEqual(s.lives, 5)

    def test_whack_escape_game_over_guard_and_stick_target(self):
        game = Game('whack'); game.event(0)
        game.update(y=1, joystick=True)
        self.assertEqual(game.state.target, 1)
        s = game.state
        s.lives = 1
        h = s.holes[1]
        h.kind, h.phase, h.pop = 0, 3, .01
        game.update()
        self.assertEqual(s.state, 2)
        game.event(0)
        self.assertEqual(game.state.state, 2)
        for _ in range(25): game.update()
        game.event(0)
        self.assertEqual(game.state.state, 1)


class PreviewTest(unittest.TestCase):
    def test_idle_and_ignored_events_do_not_request_redraw(self):
        for name in ('flappy', 'whack', 'tetris'):
            host = GameHost(ROOT / 'fs' / 'games' / f'{name}.lua')
            self.assertIs(host.call('update', .033, 0, 0, True), False)
            self.assertIs(host.call('event', 7, 0, 0), False)
            host.call('event', 0, 0, 0)
            self.assertIsNot(host.call('update', .033, 0, 0, True), False)

    def test_whack_supports_firmware_requiring_omitted_sprite_tint(self):
        host = GameHost(ROOT / 'fs' / 'games' / 'whack.lua')
        host.lua.execute('''
            local sprite = game.sprite
            game.sprite = function(...)
                assert(select('#', ...) == 5 or type(select(6, ...)) == 'number',
                       'sprite tint must be omitted or numeric')
                return sprite(...)
            end
        ''')
        host.draw()
        host.call('event', 0, 0, 0)
        for _ in range(120):
            host.call('update', .033, 0, 0, True)
            host.draw()

    def test_runaway_callback_is_stopped(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'loop.lua'
            path.write_text('-- pubmote-game {"api":1}\nfunction init() while true do end end')
            with self.assertRaisesRegex(Exception, 'instruction budget'):
                GameHost(path)

    def test_drawing_limit_is_enforced(self):
        host = GameHost(ROOT / 'fs' / 'games' / 'flappy.lua')
        for _ in range(512):
            host.rect(0, 0, 1, 1, 0)
        with self.assertRaisesRegex(ValueError, 'Drawing budget'):
            host.rect(0, 0, 1, 1, 0)


if __name__ == '__main__':
    unittest.main()
