// Extracted from library/std/src/fs.rs:2349
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fs;
        
        if let Ok(entries) = fs::read_dir(".") {
            for entry in entries {
                if let Ok(entry) = entry {
                    // Here, `entry` is a `DirEntry`.
                    if let Ok(metadata) = entry.metadata() {
                        // Now let's show our entry's permissions!
                        println!("{:?}: {:?}", entry.path(), metadata.permissions());
                    } else {
                        println!("Couldn't get metadata for {:?}", entry.path());
                    }
                }
            }
        }
        Ok(())
    }
    doctest().unwrap();
}
