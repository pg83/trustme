#![feature(auto_traits)]

auto trait Transfer {}

struct Packet<T> {
    head: T,
    items: [u16; 2],
    tag: (u8, bool),
}

fn require<T: Transfer>(value: T) -> T {
    value
}

fn main() {
    let packet = require(Packet {
        head: 367u16,
        items: [373, 379],
        tag: (17, true),
    });
    assert_eq!(packet.tag, (17, true));
    let read = require(move || packet.head + packet.items[1]);
    assert_eq!(read(), 746);
    let pointer = require(core::ptr::null::<u16>());
    assert!(pointer.is_null());
    let shared = require(&[19u8, 23]);
    assert_eq!(shared[1], 23);
    let function: fn(u16) -> u16 = require(|value| value + 1);
    assert_eq!(function(383), 384);
}
