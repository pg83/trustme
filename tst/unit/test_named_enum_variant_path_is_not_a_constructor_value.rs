//@ compile-fail: error

enum Choice {
    Named { value: u8 },
}

fn main() {
    let _constructor = Choice::Named;
}
