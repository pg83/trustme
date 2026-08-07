// Extracted from library/core/src/net/ip_addr.rs:1002
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    assert_eq!(Ipv4Addr::new(192, 0, 2, 255).is_documentation(), true);
    assert_eq!(Ipv4Addr::new(198, 51, 100, 65).is_documentation(), true);
    assert_eq!(Ipv4Addr::new(203, 0, 113, 6).is_documentation(), true);
    assert_eq!(Ipv4Addr::new(193, 34, 17, 19).is_documentation(), false);
}
