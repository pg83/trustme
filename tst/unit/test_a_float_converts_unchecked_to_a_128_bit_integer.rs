// num-traits' `ToPrimitive::to_i128` for floats checks the range and then
// calls `f.to_int_unchecked::<i128>()` (the `float_to_int_unchecked`
// intrinsic). With 128-bit integers emulated in the C backend the intrinsic
// was lowered to `abort()`; for an in-range value it truncates toward zero,
// as the `as` cast does.
use std::hint::black_box;

fn main() {
    let big: f64 = black_box(1.0e30);
    let neg: f64 = black_box(-12345.75);
    let small: f32 = black_box(3.5);
    unsafe {
        assert_eq!(big.to_int_unchecked::<i128>(), 1.0e30 as i128);
        assert_eq!(big.to_int_unchecked::<u128>(), 1.0e30 as u128);
        assert_eq!(neg.to_int_unchecked::<i128>(), -12345);
        assert_eq!(small.to_int_unchecked::<u128>(), 3);
        assert_eq!(small.to_int_unchecked::<i128>(), 3);
    }
}
