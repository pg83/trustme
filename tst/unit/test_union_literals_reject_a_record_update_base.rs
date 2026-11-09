//@ compile-fail: base value
union Storage {
    byte: u8,
    word: u16,
}

fn main() {
    let base = Storage { byte: 127 };
    let _ = Storage { byte: 131, ..base };
}
