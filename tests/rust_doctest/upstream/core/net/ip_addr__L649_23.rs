// Extracted from library/core/src/net/ip_addr.rs:649
#![allow(unused)]
#![feature(ip_as_octets)]
fn main() {

    use std::net::Ipv4Addr;

    let addr = Ipv4Addr::new(127, 0, 0, 1);
    assert_eq!(addr.as_octets(), &[127, 0, 0, 1]);
}
