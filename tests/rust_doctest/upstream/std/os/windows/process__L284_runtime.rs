// Extracted from library/std/src/os/windows/process.rs:284
#![allow(unused)]
#![feature(windows_process_extensions_async_pipes)]
fn main() {
    use std::os::windows::process::CommandExt;
    use std::process::{Command, Stdio};
    
    let program = "";
    
    Command::new(program)
        .async_pipes(true)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
}
