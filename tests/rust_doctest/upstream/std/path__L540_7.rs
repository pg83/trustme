// Extracted from library/std/src/path.rs:540
#![allow(unused)]
fn main() {
    use std::path::Path;
    
    let path = Path::new("./tmp/foo/bar.txt");
    let components: Vec<_> = path.components().map(|comp| comp.as_os_str()).collect();
    assert_eq!(&components, &[".", "tmp", "foo", "bar.txt"]);
}
