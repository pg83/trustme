//@ aux-build: nested_object_trait_aliases.rs
#![feature(trait_alias)]

struct Number(u16);
impl nested_object_trait_aliases::Read<u16> for Number {
    fn read(&self, input: u16) -> u16 { self.0 + input }
}

fn main() {
    let number = Number(257);
    let object: &dyn nested_object_trait_aliases::Second<u16> = &number;
    assert_eq!(object.read(11), 268);
}
