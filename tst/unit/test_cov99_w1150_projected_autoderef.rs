use std::ops::Deref;

trait Source {
    type Value: Deref<Target = u32>;
}

struct BoxSource;
impl Source for BoxSource {
    type Value = Box<u32>;
}

fn bit_count<S: Source>(value: S::Value) -> u32 {
    value.count_ones()
}

fn main() {
    assert_eq!(bit_count::<BoxSource>(Box::new(0b101101)), 4);
}
