//@ check-pass

#![feature(specialization)]
#![allow(incomplete_features)]

trait Writable {}

trait SpecWrite {
    fn spec_write(self) -> u8;
}

impl<T: Writable + ?Sized> SpecWrite for &mut T {
    default fn spec_write(self) -> u8 {
        1
    }
}

impl<T: Writable> SpecWrite for &mut T {
    fn spec_write(self) -> u8 {
        2
    }
}

fn write<T: Writable + ?Sized>(value: &mut T) -> u8 {
    value.spec_write()
}

impl Writable for u32 {}

fn main() {
    assert_eq!(write(&mut 0_u32), 2);
}
