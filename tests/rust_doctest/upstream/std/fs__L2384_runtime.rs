// Extracted from library/std/src/fs.rs:2384
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs;
        
        if let Ok(entries) = fs::read_dir(".") {
            for entry in entries {
                if let Ok(entry) = entry {
                    // Here, `entry` is a `DirEntry`.
                    if let Ok(file_type) = entry.file_type() {
                        // Now let's show our entry's file type!
                        println!("{:?}: {:?}", entry.path(), file_type);
                    } else {
                        println!("Couldn't get file type for {:?}", entry.path());
                    }
                }
            }
        }
        Ok(())
    }
    doctest().unwrap();
}
