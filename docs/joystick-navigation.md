# Joystick navigation

The device UI supports an up/down joystick and one button. Touch remains available.

Controls remain unfocused until the first joystick navigation movement or button
press (or a simulator navigation key). That first input highlights the default
control without activating it; subsequent inputs navigate and activate normally.
Double click still performs Back immediately. Navigation focus resets on every
screen change, including returning to a previous screen. Dialogs follow the current
screen's navigation mode. Touch alone does not enable it.
The menu remembers its selected item; the first input after returning reveals
focus on that item again.
Returning to the stats screen resets the menu selection and scroll position.

Dual-axis remotes also support left/right. Outside editing, left/up moves to the
previous control and right/down moves to the next, with no wrapping. Up/Left
stops at Back; clicking it activates the parent action. While editing, Right
increases a value and Left decreases it. Focused page dots accept Left/Right
directly to change pages. The keyboard uses all four directions to move by row
and column, including its footer actions; arrows stop at grid edges. Up/down-only
remotes keep the sequential keyboard order so every key remains reachable.

- Up/down moves focus between available controls. Holding the stick repeats after
  750 ms, then every 500 ms. Focused items in lists scroll into view. Focus stops
  at the first and last control instead of wrapping around.
  Back/save comes first at the top, followed by the page controls and footer.
  Screens with a header Back button start on the next available control. Up once
  highlights Back; further Up movements stop there. Click to return to the parent screen.
  If Back is the only available control, focus starts there.
- Click activates a button, toggles a switch, or enters/leaves value editing.
  Focus uses theme-colored fills, markers and thumb rings; filled primary buttons
  use their text color for the focus marker. Editing strengthens
  the accent fill and ring on value controls. While editing,
  up increases a value and down decreases it. Dropdown changes apply immediately.
- Focus the page dots and click to edit the page selection. Down advances a page;
  up returns to the previous page. Click again to resume moving between controls.
- The color control cycles through hue, saturation and lightness on successive
  clicks, then leaves editing. Up/down adjusts the current channel.
- Double click leaves editing first, otherwise performs the screen's back/cancel
  action. On Settings, back saves. On the ride screen, double click follows the
  saved preference: Open menu is the default, and selecting None disables it.
- Long hold retains the existing power-off behavior.

Dialogs take focus on Cancel when present, or OK for a single-action message.
Up/down moves between dialog actions and stops at either end; busy operations block input.

During joystick center, range and deadband measurement, stick navigation pauses
and the button advances the calibration. Double click cancels. Navigation resumes
for expo and axis inversion. On the riding screen the stick continues to control
the board. Returning from menus resumes joystick forwarding after the stick
returns to its calibrated neutral position. Touch the ride statistic or battery
display to cycle its presentation.

Board rows have a Delete action, so deletion does not require a touch hold.
About's version area can be clicked five times to open Extras; its diagnostics
area can be clicked to enter up/down scrolling.

Game input still uses each game's controls. Games release double click so the
button reaches the game on the press edge rather than after the double-click
window. To leave a game, click then press and hold (about 0.4 s, until the buzz),
or tap the on-screen Exit button. Holding for a full second still powers off.
Games that use left/right movement still need that axis for gameplay.
Gameplay disables UI focus navigation; the first button press goes to the game
immediately. Arrow keys in the simulator also send game
direction events. Tab does not select the game's touch Exit button.

In the desktop/browser simulator, the control window provides Up, Down, Left, Right, Click and
Double click buttons. Tab, Shift+Tab, Enter and Escape in the remote window perform
the single-axis navigation actions; arrow keys exercise dual-axis navigation.
The joystick sliders remain calibration inputs.

Run the UI behavior checks with:

```sh
cargo test --manifest-path tests/ui-gestures/Cargo.toml
python scripts/simulator.py test
```
