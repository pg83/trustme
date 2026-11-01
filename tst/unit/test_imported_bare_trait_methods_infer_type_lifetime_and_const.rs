//@ aux-build: imported_trait_parameters.rs
extern crate imported_trait_parameters;

use imported_trait_parameters::{Borrow, Convert, Count};

struct Packet<'a>(&'a u16);

impl Convert<u16> for Packet<'_> {
    type Output = u32;
    fn convert(&self, value: u16) -> u32 { *self.0 as u32 + value as u32 }
}

impl<'a> Borrow<'a> for Packet<'a> {
    fn borrow(&self) -> &'a u16 { self.0 }
}

impl Count<9> for Packet<'_> {}

fn main() {
    let value = 23u16;
    let packet = Packet(&value);
    assert_eq!(Convert::convert(&packet, 19u16), 42);
    assert_eq!(*Borrow::borrow(&packet), 23);
    assert_eq!(Count::count(&packet), 9);
}
