use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let target = env::var("TARGET").unwrap_or_default();

    // Only apply embedded linker settings for ARM targets
    if target.starts_with("thumbv") {
        // Copy memory.x to OUT_DIR so it takes precedence
        let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
        let memory_x = include_str!("memory.x");
        fs::write(out_dir.join("memory.x"), memory_x).unwrap();

        println!("cargo:rustc-link-search={}", out_dir.display());

        // Tell the linker to use link.x script
        println!("cargo:rustc-link-arg=--nmagic");
        println!("cargo:rustc-link-arg=-Tlink.x");
        println!("cargo:rustc-link-arg=-Tdefmt.x");

        println!("cargo:rerun-if-changed=memory.x");
    }

    println!("cargo:rerun-if-changed=build.rs");
}
