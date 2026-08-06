// Extracted from library/core/src/net/ip_addr.rs:720
#![allow(unused)]
fn main() {
    use std::net::Ipv4Addr;
    
    assert_eq!(Ipv4Addr::new(10, 0, 0, 1).is_private(), true);
    assert_eq!(Ipv4Addr::new(10, 10, 10, 10).is_private(), true);
    assert_eq!(Ipv4Addr::new(172, 16, 10, 10).is_private(), true);
    assert_eq!(Ipv4Addr::new(172, 29, 45, 14).is_private(), true);
    assert_eq!(Ipv4Addr::new(172, 32, 0, 2).is_private(), false);
    assert_eq!(Ipv4Addr::new(192, 168, 0, 2).is_private(), true);
    assert_eq!(Ipv4Addr::new(192, 169, 0, 2).is_private(), false);
}
