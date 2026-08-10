// Extracted from library/core/src/net/ip_addr.rs:585
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    let addr = Ipv4Addr::UNSPECIFIED;
    assert_eq!(addr, Ipv4Addr::new(0, 0, 0, 0));
}
