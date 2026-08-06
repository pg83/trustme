// Extracted from library/std/src/path.rs:583
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::path::Path;
        
        let path = Path::new("/tmp/foo/bar.txt");
        
        for component in path.components() {
            println!("{component:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
