// Extracted from library/core/src/net/ip_addr.rs:1966
#![allow(unused)]
fn main() {
    use std::net::{Ipv4Addr, Ipv6Addr};

    assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).to_ipv4(), None);
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).to_ipv4(),
               Some(Ipv4Addr::new(192, 10, 2, 255)));
    assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1).to_ipv4(),
               Some(Ipv4Addr::new(0, 0, 0, 1)));
}
