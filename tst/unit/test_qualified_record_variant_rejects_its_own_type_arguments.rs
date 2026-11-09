//@ compile-fail: Type parameters on UFCS enum variant
#![feature(more_qualified_paths)]

enum Choice<T> {
    Value { value: T },
}

fn main() {
    let _ = <Choice<u8>>::Value::<u16> { value: 199u8 };
}
