// Extracted from library/std/src/thread/mod.rs:2038
#![allow(dead_code)]
use std::{io, thread};

fn main() -> io::Result<()> {
    let count = thread::available_parallelism()?.get();
    assert!(count >= 1_usize);
    Ok(())
}
