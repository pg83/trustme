//@ aux-build: imported_trait_parameters.rs
extern crate imported_trait_parameters;

mod facade {
    pub use imported_trait_parameters::{Borrow, Convert, Count};
}

struct Packet<'a>(&'a u16);

impl facade::Convert<u16> for Packet<'_> {
    type Output = u32;
    fn convert(&self, value: u16) -> u32 { *self.0 as u32 * value as u32 }
}

impl<'a> facade::Borrow<'a> for Packet<'a> {
    fn borrow(&self) -> &'a u16 { self.0 }
}

impl facade::Count<11> for Packet<'_> {}

fn main() {
    let value = 6u16;
    let packet = Packet(&value);
    assert_eq!(crate::facade::Convert::convert(&packet, 7u16), 42);
    assert_eq!(*crate::facade::Borrow::borrow(&packet), 6);
    assert_eq!(crate::facade::Count::count(&packet), 11);
}
