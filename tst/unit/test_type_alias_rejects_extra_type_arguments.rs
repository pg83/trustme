//@ compile-fail: Mismatched type-generic count

type Pair<T, U> = (T, U);

fn inspect(_: Pair<u8, u16, u32>) {}

fn main() {}
