#![feature(type_alias_impl_trait)]

type Hidden = impl Copy + PartialEq<u8>;

#[define_opaque(Hidden)]
fn make(value: u8) -> Hidden {
    value
}

fn replace(destination: &mut Hidden, value: Hidden) {
    *destination = value;
}

fn main() {
    let mut value = make(3);
    replace(&mut value, make(9));
    assert!(value == 9u8);
}
