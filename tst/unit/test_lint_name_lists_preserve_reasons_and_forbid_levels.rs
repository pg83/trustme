#![forbid(unused_variables, unused_mut, reason = "keep crate restrictions")]

#[allow(dead_code, reason = "exercise a disjoint item override")]
fn increment(value: u8) -> u8 {
    value + 1
}

fn main() {
    let value = increment(12);
    assert_eq!(value, 13);
}
