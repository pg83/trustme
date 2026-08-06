// Extracted from library/std/src/io/error.rs:656
#![allow(unused)]
fn main() {
    if cfg!(target_os = "linux") {
    use std::io;
    
    let error = io::Error::from_raw_os_error(22);
    assert_eq!(error.kind(), io::ErrorKind::InvalidInput);
    }
}
