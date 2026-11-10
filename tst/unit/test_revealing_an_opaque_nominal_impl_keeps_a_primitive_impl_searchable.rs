#![feature(type_alias_impl_trait)]

#[derive(Clone, Copy)]
struct Payload;

type Stored = impl Copy;

trait Read {
    fn read(&self) -> u8;
}

impl Read for Stored {
    fn read(&self) -> u8 {
        17
    }
}

impl Read for u16 {
    fn read(&self) -> u8 {
        *self as u8
    }
}

#[define_opaque(Stored)]
fn stored() -> Stored {
    Payload
}

fn main() {
    let value = stored();
    assert_eq!(value.read(), 17);
    assert_eq!(23_u16.read(), 23);
}
