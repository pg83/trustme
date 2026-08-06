// Extracted from library/std/src/os/unix/net/addr.rs:155
#![allow(unused)]
fn main() {
    use std::os::unix::net::SocketAddr;
    
    assert!(SocketAddr::from_pathname("/path/with/\0/bytes").is_err());
}
