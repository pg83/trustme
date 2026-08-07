// Extracted from library/core/src/net/ip_addr.rs:1098
#![allow(unused)]
fn main() {
    use std::net::{IpAddr, Ipv4Addr};

    let addr = Ipv4Addr::new(127, 0, 0, 1);

    assert_eq!(
        IpAddr::V4(addr),
        IpAddr::from(addr)
    )
}
