// Extracted from library/std/src/path.rs:1074
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let path = Path::new("/foo/bar");
    
    for ancestor in path.ancestors() {
        println!("{}", ancestor.display());
    }
}
