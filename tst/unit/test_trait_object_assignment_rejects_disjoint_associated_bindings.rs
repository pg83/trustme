//@ compile-fail: Type mismatch between

trait Family {
    type Left;
    type Right;
}

fn reject(source: &dyn Family<Left = u8>) {
    let _: &dyn Family<Right = u8> = source;
}

fn main() {}
