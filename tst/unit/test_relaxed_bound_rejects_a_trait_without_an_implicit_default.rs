//@ compile-fail: MaybeTrait on unknown trait
fn invalid<T: ?Clone>(value: T) -> T { value }
fn main() { assert_eq!(invalid(47u32), 47); }
