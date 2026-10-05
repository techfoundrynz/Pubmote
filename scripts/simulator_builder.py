"""PlatformIO targets for the desktop Slint simulator."""
import subprocess
from pathlib import Path
from SCons.Script import AlwaysBuild, Default, DefaultEnvironment

env = DefaultEnvironment()
root = Path(env.subst("$PROJECT_DIR"))
runner = root / "scripts" / "simulator.py"


def action(command):
    def run(source, target, env):
        return subprocess.call([env.subst("$PYTHONEXE"), str(runner), command], cwd=root)
    return run


build = env.Alias("buildprog", [], action("build"))
AlwaysBuild(build)
Default(build)
for name, command, title in (
    ("simulate", "run", "Run desktop simulator"),
    ("exec", "run", "Run desktop simulator"),
    ("simulator-test", "test", "Check UI and simulator callbacks without a display"),
):
    env.AddCustomTarget(name, None, action(command), title=title)
