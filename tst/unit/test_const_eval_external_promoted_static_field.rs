use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

fn main() {
    assert_eq!(IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0)).is_unspecified(), true);
    assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0)).is_unspecified(), true);
}
