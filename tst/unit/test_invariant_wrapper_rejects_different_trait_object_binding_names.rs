//@ compile-fail: associated bounds differ

trait Fields {
    type Left;
    type Right;
}

struct Wrapper<T>(T);

fn compare(value: Wrapper<&dyn Fields<Left = u8>>) {
    let _: Wrapper<&dyn Fields<Right = u8>> = value;
}

fn main() {}
