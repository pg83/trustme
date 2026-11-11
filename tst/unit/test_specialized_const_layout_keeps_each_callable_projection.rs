#![feature(specialization, generic_const_exprs)]
#![allow(incomplete_features)]

trait Shape {
    type Payload;
}

impl<T> Shape for T {
    default type Payload = T;
}

impl<T: FnOnce() -> u16> Shape for T {
    type Payload = (T, u8);
}

fn package<F>(value: F) -> (F, [u8; std::mem::size_of::<<F as Shape>::Payload>()])
where
    [(); std::mem::size_of::<<F as Shape>::Payload>()]:,
{
    (value, [0x5a; std::mem::size_of::<<F as Shape>::Payload>()])
}

fn marker() -> u16 {
    0x1234
}

fn concealed() -> impl Fn() -> u16 + Copy {
    marker
}

fn main() {
    let (item, bytes) = package(marker);
    assert_eq!(item(), 0x1234);
    assert_eq!(bytes, [0x5a]);

    let (pointer, bytes) = package(marker as fn() -> u16);
    assert_eq!(pointer(), 0x1234);
    assert_eq!(bytes.len(), std::mem::size_of::<(fn() -> u16, u8)>());
    assert!(bytes.iter().all(|byte| *byte == 0x5a));

    let (closure, bytes) = package(|| 73u16);
    assert_eq!(closure(), 73);
    assert_eq!(bytes, [0x5a]);

    let (opaque, bytes) = package(concealed());
    assert_eq!(opaque(), 0x1234);
    assert_eq!(bytes, [0x5a]);
}
