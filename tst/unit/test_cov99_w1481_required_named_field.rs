//@ compile-fail: Field 'missing' not specified
//@ compile-flags: -Cpanic=abort

struct Required {
    present: u8,
    missing: u8,
}

fn main() {
    let _ = Required { present: 1 };
}
