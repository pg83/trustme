#![feature(type_alias_impl_trait)]

type Operation = impl Fn(u16) -> u16 + Copy;

fn plus_three(value: u16) -> u16 {
    value + 3
}

#[define_opaque(Operation)]
fn make() -> Operation {
    plus_three
}

trait Layout {
    type Value;
    const WIDTH: usize = core::mem::size_of::<Self::Value>();
}

impl Layout for () {
    type Value = Operation;
}

const WIDTH: usize = <() as Layout>::WIDTH;

fn main() {
    let operation = make();
    let copy = operation;
    assert_eq!(operation(907), 910);
    assert_eq!(copy(911), 914);
    assert_eq!(WIDTH, 0);
    let _: [u8; WIDTH] = [];
}
