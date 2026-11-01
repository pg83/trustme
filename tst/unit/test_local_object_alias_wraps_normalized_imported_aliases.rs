//@ aux-build: normalized_object_aliases.rs
#![feature(trait_alias)]

trait Local = normalized_object_aliases::Second<u8, Item = u16>;

struct Number(u16);
impl normalized_object_aliases::Read<u8> for Number {
    type Item = u16;
    fn read(&self, value: u8) -> u16 { self.0 * value as u16 }
}

fn main() {
    let number = Number(6);
    let object: &dyn Local = &number;
    assert_eq!(object.read(7), 42);
    let direct: &dyn normalized_object_aliases::First<u8, Item = u16> = &number;
    assert_eq!(direct.read(3), 18);
}
