use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    fs::write(out.join("generated.rs"), "pub const GENERATED: usize = 42;\n").unwrap();
    fs::write(out.join("native.cpp"), "extern \"C\" int graph_native() { return 42; }\n").unwrap();
    let object = out.join("native.o");
    let status = Command::new(env::var("CXX").unwrap_or_else(|_| "c++".to_owned()))
        .args(["-x", "c++", "-c"])
        .arg(out.join("native.cpp"))
        .arg("-o")
        .arg(&object)
        .status()
        .unwrap();
    assert!(status.success());
    let archive = out.join("libgraph_native.a");
    let status = Command::new(env::var("AR").unwrap_or_else(|_| "ar".to_owned()))
        .arg("crs")
        .arg(&archive)
        .arg(&object)
        .status()
        .unwrap();
    assert!(status.success());
    println!("cargo:rustc-cfg=from_build_script");
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=graph_native");
}
