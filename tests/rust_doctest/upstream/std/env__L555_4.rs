// Extracted from library/std/src/env.rs:555
#![allow(unused)]
fn main() {
    if cfg!(unix) {
    use std::env;
    use std::path::Path;
    
    let paths = [Path::new("/bin"), Path::new("/usr/bi:n")];
    assert!(env::join_paths(paths.iter()).is_err());
    }
}
