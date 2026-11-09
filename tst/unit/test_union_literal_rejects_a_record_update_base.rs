//@ compile-fail: error

union Number {
    small: u8,
    wide: u32,
}

fn main() {
    let base = Number { small: 7 };
    let _updated = Number { wide: 11, ..base };
}
