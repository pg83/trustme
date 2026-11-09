//@ compile-fail: error

struct Record {
    value: u8,
}

fn main() {
    let _value = Record;
}
