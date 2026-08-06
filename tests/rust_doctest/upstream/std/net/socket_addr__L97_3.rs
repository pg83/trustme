// Extracted from library/std/src/net/socket_addr.rs:97
#![allow(unused)]
fn main() {
    use std::io;
    use std::net::ToSocketAddrs;
    
    let err = "127.0.0.1".to_socket_addrs().unwrap_err();
    assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
}
