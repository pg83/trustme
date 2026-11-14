#![feature(coerce_unsized)]

use std::ops::CoerceUnsized;

struct Relay<P>(P);

impl<P: CoerceUnsized<Q>, Q> CoerceUnsized<Relay<Q>> for Relay<P> {}

struct Packet<P> {
    relay: Relay<P>,
    tag: u8,
}

impl<P: CoerceUnsized<Q>, Q> CoerceUnsized<Packet<Q>> for Packet<P> {}

fn main() {
    let values = [11u16, 23, 47];
    let source = Packet { relay: Relay(&values), tag: 9 };
    let target: Packet<&[u16]> = source;

    assert_eq!(target.tag, 9);
    assert_eq!(target.relay.0.len(), 3);
    assert_eq!(target.relay.0[1], 23);
}
