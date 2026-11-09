//@ compile-fail: error

enum Choice {
    Named { value: u8 },
}

fn main() {
    let _value = Choice::Named(7);
}
