// Extracted from library/core/src/net/ip_addr.rs:672
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    assert_eq!(Ipv4Addr::new(0, 0, 0, 0).is_unspecified(), true);
    assert_eq!(Ipv4Addr::new(45, 22, 13, 197).is_unspecified(), false);
}
