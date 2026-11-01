//@ aux-build: normalized_object_aliases.rs
#![feature(trait_alias)]

struct Number(u16);
impl normalized_object_aliases::Read<u8> for Number {
    type Item = u16;
    fn read(&self, value: u8) -> u16 { self.0 + value as u16 }
}

fn main() {
    let object: Box<dyn normalized_object_aliases::Second<u8, Item = u16>> = Box::new(Number(30));
    assert_eq!(object.read(12), 42);
    let markers: &dyn normalized_object_aliases::Markers = &*object;
    assert_eq!(core::mem::size_of_val(markers), 2);
}
