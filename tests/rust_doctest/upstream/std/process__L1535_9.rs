// Extracted from library/std/src/process.rs:1535
#![allow(unused)]
#![feature(stdio_makes_pipe)]
fn main() {
    use std::process::Stdio;

    let io = Stdio::piped();
    assert_eq!(io.makes_pipe(), true);
}
