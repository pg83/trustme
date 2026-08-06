// Extracted from library/std/src/io/error.rs:667
#![allow(unused)]
fn main() {
    if cfg!(windows) {
    use std::io;
    
    let error = io::Error::from_raw_os_error(10022);
    assert_eq!(error.kind(), io::ErrorKind::InvalidInput);
    }
}
