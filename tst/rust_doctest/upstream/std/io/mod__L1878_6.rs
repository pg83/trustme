// Extracted from library/std/src/io/mod.rs:1878
#![feature(write_all_vectored)]
fn main() -> std::io::Result<()> {

use std::io::{Write, IoSlice};

let mut writer = Vec::new();
let bufs = &mut [
    IoSlice::new(&[1]),
    IoSlice::new(&[2, 3]),
    IoSlice::new(&[4, 5, 6]),
];

writer.write_all_vectored(bufs)?;
// Note: the contents of `bufs` is now undefined, see the Notes section.

assert_eq!(writer, &[1, 2, 3, 4, 5, 6]);
Ok(()) }
