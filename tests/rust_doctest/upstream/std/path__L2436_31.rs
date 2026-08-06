// Extracted from library/std/src/path.rs:2436
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let path = Path::new("/foo/bar");
    let parent = path.parent().unwrap();
    assert_eq!(parent, Path::new("/foo"));
    
    let grand_parent = parent.parent().unwrap();
    assert_eq!(grand_parent, Path::new("/"));
    assert_eq!(grand_parent.parent(), None);
    
    let relative_path = Path::new("foo/bar");
    let parent = relative_path.parent();
    assert_eq!(parent, Some(Path::new("foo")));
    let grand_parent = parent.and_then(Path::parent);
    assert_eq!(grand_parent, Some(Path::new("")));
    let great_grand_parent = grand_parent.and_then(Path::parent);
    assert_eq!(great_grand_parent, None);
}
