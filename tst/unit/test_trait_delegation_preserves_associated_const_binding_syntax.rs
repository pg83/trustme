#![feature(fn_delegation, associated_const_equality)]
#![allow(incomplete_features)]

trait Tagged {
    const CODE: u16;
    fn value(&self) -> u16;
}

struct Header(u16);

impl Tagged for Header {
    const CODE: u16 = 83;
    fn value(&self) -> u16 { self.0 }
}

trait Inspect: Sized {
    fn read<T: Tagged<CODE = { 83 }>>(self, source: &T) -> (Self, u16) {
        (self, source.value())
    }
}

impl Inspect for u32 {}

reuse Inspect::read;

fn main() {
    assert_eq!(Header::CODE, 83);
    assert_eq!(read(61u32, &Header(67)), (61, 67));
}
