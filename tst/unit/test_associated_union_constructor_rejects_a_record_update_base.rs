//@ compile-fail: base value
#![feature(more_qualified_paths)]

union Bits {
    byte: u8,
    word: u16,
}

trait Repr {
    type Data;
}

impl Repr for () {
    type Data = Bits;
}

fn main() {
    let base = Bits { byte: 139 };
    let _ = <() as Repr>::Data { byte: 149, ..base };
}
