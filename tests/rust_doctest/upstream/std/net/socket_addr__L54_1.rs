// Extracted from library/std/src/net/socket_addr.rs:54
#![allow(unused)]
fn main() {
    use std::net::{ToSocketAddrs, SocketAddr};
    
    let addr = SocketAddr::from(([127, 0, 0, 1], 443));
    let mut addrs_iter = addr.to_socket_addrs().unwrap();
    
    assert_eq!(Some(addr), addrs_iter.next());
    assert!(addrs_iter.next().is_none());
}
