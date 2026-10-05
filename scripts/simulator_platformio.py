"""Use Cargo as the native environment's builder, without invoking GCC."""
from pathlib import Path

Import("env")

env.Replace(BUILD_SCRIPT=str(Path(env.subst("$PROJECT_DIR")) / "scripts" / "simulator_builder.py"))
