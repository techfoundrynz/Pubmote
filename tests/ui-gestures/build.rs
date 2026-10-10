fn main() {
    // The firmware UI requires embedded image resources in the Slint macro.
    println!("cargo:rustc-env=SLINT_EMBED_TEXTURES=1");
}
