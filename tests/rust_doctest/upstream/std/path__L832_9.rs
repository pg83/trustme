// Extracted from library/std/src/path.rs:832
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let mut iter = Path::new("/tmp/foo/bar.txt").iter();
    iter.next();
    iter.next();
    
    assert_eq!(Path::new("foo/bar.txt"), iter.as_path());
}
