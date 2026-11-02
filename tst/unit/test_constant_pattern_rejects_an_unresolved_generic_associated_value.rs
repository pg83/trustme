//@ compile-fail: constant
trait Known { const VALUE: Self; }
impl Known for u8 { const VALUE: Self = 23; }
fn classify<T: Known + PartialEq>(value: T) -> bool { matches!(value, T::VALUE) }
fn main() { let _ = classify(23u8); }
