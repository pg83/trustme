// Extracted from library/std/src/net/socket_addr.rs:80
#![allow(unused)]
fn main() {
    use std::net::{SocketAddr, ToSocketAddrs};

    let addr1 = SocketAddr::from(([0, 0, 0, 0], 80));
    let addr2 = SocketAddr::from(([127, 0, 0, 1], 443));
    let addrs = vec![addr1, addr2];

    let mut addrs_iter = (&addrs[..]).to_socket_addrs().unwrap();

    assert_eq!(Some(addr1), addrs_iter.next());
    assert_eq!(Some(addr2), addrs_iter.next());
    assert!(addrs_iter.next().is_none());
}
