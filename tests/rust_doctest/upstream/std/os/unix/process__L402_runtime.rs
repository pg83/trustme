// Extracted from library/std/src/os/unix/process.rs:402
#![feature(unix_send_signal)]

use std::{io, os::unix::process::ChildExt, process::{Command, Stdio}};

use libc::SIGTERM;

fn main() -> io::Result<()> {
    let child = Command::new("cat").stdin(Stdio::piped()).spawn()?;
    child.send_signal(SIGTERM)?;
    Ok(())
}
