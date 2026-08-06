// Extracted from library/core/src/net/ip_addr.rs:1718
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;
    
    // The loopback address (`::1`) does not actually have link-local scope.
    assert_eq!(Ipv6Addr::LOCALHOST.is_unicast_link_local(), false);
    
    // Only addresses in `fe80::/10` have link-local scope.
    assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_unicast_link_local(), false);
    assert_eq!(Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 0).is_unicast_link_local(), true);
    
    // Addresses outside the stricter `fe80::/64` also have link-local scope.
    assert_eq!(Ipv6Addr::new(0xfe80, 0, 0, 1, 0, 0, 0, 0).is_unicast_link_local(), true);
    assert_eq!(Ipv6Addr::new(0xfe81, 0, 0, 0, 0, 0, 0, 0).is_unicast_link_local(), true);
}
