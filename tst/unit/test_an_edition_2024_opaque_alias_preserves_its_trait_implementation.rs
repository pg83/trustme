//@ edition: 2024
#![feature(type_alias_impl_trait)]

type Secret = impl Copy;

trait Read {
    fn read(&self) -> u8;
}

impl Read for Secret {
    fn read(&self) -> u8 {
        37
    }
}

#[define_opaque(Secret)]
fn make() -> Secret {
    11_u8
}

fn main() {
    let first = make();
    let copied = first;
    assert_eq!(copied.read(), 37);
    assert_eq!(first.read(), 37);
}
