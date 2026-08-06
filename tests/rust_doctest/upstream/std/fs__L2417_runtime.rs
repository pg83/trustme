// Extracted from library/std/src/fs.rs:2417
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs;
        
        if let Ok(entries) = fs::read_dir(".") {
            for entry in entries {
                if let Ok(entry) = entry {
                    // Here, `entry` is a `DirEntry`.
                    println!("{:?}", entry.file_name());
                }
            }
        }
        Ok(())
    }
    doctest().unwrap();
}
