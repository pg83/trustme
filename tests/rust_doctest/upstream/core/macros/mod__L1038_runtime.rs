// Extracted from library/core/src/macros/mod.rs:1038
#![allow(unused)]
fn main() {
    let path: &'static str = env!("PATH");
    println!("the $PATH variable at the time of compiling was: {path}");
}
