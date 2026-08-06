// Extracted from library/std/src/path.rs:2949
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let path = Path::new("/tmp/foo.rs");
    
    println!("{}", path.display());
}
