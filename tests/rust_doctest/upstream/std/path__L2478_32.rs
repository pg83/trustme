// Extracted from library/std/src/path.rs:2478
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let mut ancestors = Path::new("/foo/bar").ancestors();
    assert_eq!(ancestors.next(), Some(Path::new("/foo/bar")));
    assert_eq!(ancestors.next(), Some(Path::new("/foo")));
    assert_eq!(ancestors.next(), Some(Path::new("/")));
    assert_eq!(ancestors.next(), None);
    
    let mut ancestors = Path::new("../foo/bar").ancestors();
    assert_eq!(ancestors.next(), Some(Path::new("../foo/bar")));
    assert_eq!(ancestors.next(), Some(Path::new("../foo")));
    assert_eq!(ancestors.next(), Some(Path::new("..")));
    assert_eq!(ancestors.next(), Some(Path::new("")));
    assert_eq!(ancestors.next(), None);
}
