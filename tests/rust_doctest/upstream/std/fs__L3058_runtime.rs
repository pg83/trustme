// Extracted from library/std/src/fs.rs:3058
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::io;
        use std::fs::{self, DirEntry};
        use std::path::Path;
        
        // one possible implementation of walking a directory only visiting files
        fn visit_dirs(dir: &Path, cb: &dyn Fn(&DirEntry)) -> io::Result<()> {
            if dir.is_dir() {
                for entry in fs::read_dir(dir)? {
                    let entry = entry?;
                    let path = entry.path();
                    if path.is_dir() {
                        visit_dirs(&path, cb)?;
                    } else {
                        cb(&entry);
                    }
                }
            }
            Ok(())
        }
        Ok(())
    }
    doctest().unwrap();
}
