// Extracted from library/core/src/net/ip_addr.rs:752
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;

    assert_eq!(Ipv4Addr::new(169, 254, 0, 0).is_link_local(), true);
    assert_eq!(Ipv4Addr::new(169, 254, 10, 65).is_link_local(), true);
    assert_eq!(Ipv4Addr::new(16, 89, 10, 65).is_link_local(), false);
}
