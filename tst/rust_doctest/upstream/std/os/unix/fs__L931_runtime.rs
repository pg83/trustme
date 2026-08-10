// Extracted from library/std/src/os/unix/fs.rs:931
#![feature(dir_entry_ext2)]
use std::os::unix::fs::DirEntryExt2;
use std::{fs, io};

fn main() -> io::Result<()> {
    let mut entries = fs::read_dir(".")?.collect::<Result<Vec<_>, io::Error>>()?;
    entries.sort_unstable_by(|a, b| a.file_name_ref().cmp(b.file_name_ref()));

    for p in entries {
        println!("{p:?}");
    }

    Ok(())
}
