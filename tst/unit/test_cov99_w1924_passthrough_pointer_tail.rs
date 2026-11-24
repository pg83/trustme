//@ edition: 2021
#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct Record<H, T: ?Sized> {
    header: H,
    tail: T,
}

struct Pointer<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Pointer<U>> for Pointer<T> {}

struct Transit<P> {
    field: P,
    tag: u8,
}
impl<P: CoerceUnsized<Q>, Q> CoerceUnsized<Transit<Q>> for Transit<P> {}

struct Envelope<H, T: ?Sized> {
    transit: Transit<Pointer<Record<H, T>>>,
    seal: u16,
}
impl<H, T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Envelope<H, U>> for Envelope<H, T> {}

fn main() {
    let record = Record { header: 0x1924u32, tail: [43u16, 61, 79, 97] };
    let source: Envelope<u32, [u16; 4]> = Envelope {
        transit: Transit { field: Pointer(&record), tag: 37 },
        seal: 521,
    };
    let target: Envelope<u32, [u16]> = source;
    let view = unsafe { &*target.transit.field.0 };

    assert_eq!(target.seal, 521);
    assert_eq!(target.transit.tag, 37);
    assert_eq!(view.header, 0x1924);
    assert_eq!(view.tail.len(), 4);
    assert_eq!(&view.tail, &record.tail[..]);
    assert_eq!(view.tail.as_ptr(), record.tail.as_ptr());
}
