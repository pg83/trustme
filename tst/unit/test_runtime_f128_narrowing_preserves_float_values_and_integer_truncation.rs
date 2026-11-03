//@ compile-variant: -Zmir-opt-level=0
#![feature(f128)]
#[inline(never)]
fn narrow(value: f128) -> (f32, f64, i32, u32) {
    (value as f32, value as f64, value as i32, value as u32)
}
fn main() {
    let positive = narrow(std::hint::black_box(577.75f128));
    assert_eq!(positive, (577.75, 577.75, 577, 577));
    let negative = narrow(std::hint::black_box(-587.5f128));
    assert_eq!(negative, (-587.5, -587.5, -587, 0));
    assert_eq!(narrow(std::hint::black_box(f128::NAN)).2, 0);
    assert_eq!(narrow(std::hint::black_box(f128::INFINITY)).3, u32::MAX);
}
