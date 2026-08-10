// Extracted from library/core/src/result.rs:1097
#![allow(unused)]
fn main() {
    let path = std::env::var("IMPORTANT_PATH")
        .expect("env variable `IMPORTANT_PATH` should be set by `wrapper_script.sh`");
}
