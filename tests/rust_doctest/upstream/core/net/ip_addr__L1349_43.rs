// Extracted from library/core/src/net/ip_addr.rs:1349
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;

    let addr = Ipv6Addr::new(
        0x1020, 0x3040, 0x5060, 0x7080,
        0x90A0, 0xB0C0, 0xD0E0, 0xF00D,
    );
    let addr_bits = addr.to_bits() & 0xffffffffffffffffffffffffffff0000_u128;
    assert_eq!(
        Ipv6Addr::new(
            0x1020, 0x3040, 0x5060, 0x7080,
            0x90A0, 0xB0C0, 0xD0E0, 0x0000,
        ),
        Ipv6Addr::from_bits(addr_bits));
}
