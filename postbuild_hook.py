Import("env")
import os
import time
import zipfile
import shutil

def zip_build_files(source, target, env):
    # Define relevant paths
    project_dir = os.getcwd()
    build_dir = os.path.join(project_dir, f".pio{os.sep}build")

    # Define list of files to zip
    files_to_zip = [
        "bootloader.bin",
        "partitions.bin",
        "firmware.bin",
        "firmware.elf"
    ]

    # Get build flags for parsing
    build_flags = env.get('BUILD_FLAGS', [])

    # Retrieve build information
    release_variant = "dev"
    version_major = 0
    version_minor = 0
    version_patch = 0

    for flag in build_flags:
        if "RELEASE_VARIANT=" in flag:
            # Extract the value after the equals sign
            release_variant = flag.split("=", 1)[1].strip('\\"')
        elif "VERSION_MAJOR=" in flag:
            # Extract the value after the equals sign
            version_major = flag.split("=", 1)[1]
        elif "VERSION_MINOR=" in flag:
            # Extract the value after the equals sign
            version_minor = flag.split("=", 1)[1]
        elif "VERSION_PATCH=" in flag:
            # Extract the value after the equals sign
            version_patch = flag.split("=", 1)[1]

    version_string = f"v{version_major}.{version_minor}.{version_patch}.{release_variant}"
    
    # Build debugging
    print(f"Creating ZIP for release of {env['PIOENV']}: {env['PIOENV']}-{version_string}.zip")
    
    # Create zip in the root of the directory
    ZipFile = zipfile.ZipFile(f"{project_dir}{os.sep}{env['PIOENV']}-{version_string}.zip", "w" )
        
    #  Zip relevant files
    for file in files_to_zip: 
        ZipFile.write(os.path.join(build_dir, env['PIOENV'], file), arcname=os.path.basename(file), compress_type=zipfile.ZIP_DEFLATED)

    # Close stream
    ZipFile.close()
    
    # Copy firmware.bin to versioned filename
    firmware_source = os.path.join(build_dir, env['PIOENV'], "firmware.bin")
    firmware_dest = f"{project_dir}{os.sep}{env['PIOENV']}-{version_string}.bin"
    
    print(f"Copying firmware.bin to: {env['PIOENV']}-{version_string}.bin")
    shutil.copy(firmware_source, firmware_dest)

    # Copy firmware.elf to versioned filename
    elf_source = os.path.join(build_dir, env['PIOENV'], "firmware.elf")
    elf_dest = f"{project_dir}{os.sep}{env['PIOENV']}-{version_string}.elf"

    if os.path.exists(elf_source):
        print(f"Copying firmware.elf to: {env['PIOENV']}-{version_string}.elf")
        shutil.copy(elf_source, elf_dest)

# Add postbuild call
env.AddPostAction("$BUILD_DIR/firmware.bin", zip_build_files)