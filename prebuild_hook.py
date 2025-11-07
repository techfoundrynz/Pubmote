Import("env")
from datetime import datetime
import hashlib

major_version = 0
minor_version = 6
patch_version = 9

def generate_build_id():
    # Get current timestamp
    timestamp = datetime.now().strftime("%Y%m%d")

    env_name = env["PIOENV"]

    # Create version string
    version = f"{major_version}.{minor_version}.{patch_version}"
    
    # Combine version and timestamp for hashing
    content_to_hash = f"{env_name}_{version}_{timestamp}"

    # Create a hash of timestamp for shorter unique ID
    hash_object = hashlib.md5(content_to_hash.encode())
    build_hash = hash_object.hexdigest()[:8]
    
    # Combine timestamp and hash
    build_id = f"{build_hash}"
    return build_id

# Add build ID to environment
hw_type = env["PIOENV"]
build_id = generate_build_id()

env.Append(BUILD_FLAGS=[f'-D HW_TYPE=\\"{hw_type}\\"'])
env.Append(BUILD_FLAGS=[f'-D BUILD_ID=\\"{build_id}\\"'])
env.Append(BUILD_FLAGS=[f'-D VERSION_MAJOR={major_version}'])
env.Append(BUILD_FLAGS=[f'-D VERSION_MINOR={minor_version}'])
env.Append(BUILD_FLAGS=[f'-D VERSION_PATCH={patch_version}'])