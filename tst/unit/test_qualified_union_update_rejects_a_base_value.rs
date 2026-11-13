//@ compile-fail: error:0: Union can't have a base value
#![feature(more_qualified_paths)]

union Storage {
    value: u16,
}

trait Factory {
    type Output;
}

impl Factory for () {
    type Output = Storage;
}

fn main() {
    let source = Storage { value: 839 };
    let _ = <() as Factory>::Output {
        value: 841,
        ..source
    };
}
