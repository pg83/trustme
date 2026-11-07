//@ compile-fail: Type mismatch between

fn identity<T>(value: T) -> T {
    value
}

fn require<T>(_: fn(T) -> [T; 1], _: T) {}

fn main() {
    require(identity, 103u16);
}
