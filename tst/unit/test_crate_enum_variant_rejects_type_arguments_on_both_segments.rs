//@ compile-fail: Type parameters were not expected here (enum params go on the variant)
enum Signal<T> { Value(T) }
fn main() { let _ = crate::Signal::<u8>::Value::<u16>(7); }
