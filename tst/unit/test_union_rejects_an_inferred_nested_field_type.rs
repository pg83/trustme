//@ compile-fail: not allowed in item signatures
union Inferred { value: core::mem::ManuallyDrop<Option<_>> }
fn main() {}
