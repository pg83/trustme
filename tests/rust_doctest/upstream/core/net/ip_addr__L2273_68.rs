// Extracted from library/core/src/net/ip_addr.rs:2273
#![allow(unused)]
fn main() {
    use std::net::Ipv6Addr;

    let addr = Ipv6Addr::from([
        0x20du16, 0x20cu16, 0x20bu16, 0x20au16,
        0x209u16, 0x208u16, 0x207u16, 0x206u16,
    ]);
    assert_eq!(
        Ipv6Addr::new(
            0x20d, 0x20c, 0x20b, 0x20a,
            0x209, 0x208, 0x207, 0x206,
        ),
        addr
    );
}
