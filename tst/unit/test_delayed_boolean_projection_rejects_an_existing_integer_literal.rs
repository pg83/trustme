//@ compile-fail: Type unificiation of integer literal with non-integer

trait Output {
    type Value;
    fn value(self) -> Self::Value;
}
struct Bit;
impl Output for Bit {
    type Value = bool;
    fn value(self) -> bool { false }
}
fn produce<T: Output>(source: T) -> T::Value {
    source.value()
}
fn main() {
    let mut result = 359;
    result = produce(Bit);
    let _ = result;
}
