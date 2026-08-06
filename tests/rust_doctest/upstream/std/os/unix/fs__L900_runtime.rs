// Extracted from library/std/src/os/unix/fs.rs:900
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs;
        use std::os::unix::fs::DirEntryExt;
        
        if let Ok(entries) = fs::read_dir(".") {
            for entry in entries {
                if let Ok(entry) = entry {
                    // Here, `entry` is a `DirEntry`.
                    println!("{:?}: {}", entry.file_name(), entry.ino());
                }
            }
        }
        Ok(())
    }
    doctest().unwrap();
}
