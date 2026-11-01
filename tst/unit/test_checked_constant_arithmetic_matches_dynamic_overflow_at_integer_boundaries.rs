//@ compile-variant: -O
#![feature(core_intrinsics)]
macro_rules! check {
    ($ty:ty, $left:expr, $right:expr, $add:expr, $sub:expr, $mul:expr) => {{
        let left: $ty = $left;
        let right: $ty = $right;
        let sum = std::intrinsics::add_with_overflow(left, right);
        let difference = std::intrinsics::sub_with_overflow(left, right);
        let product = std::intrinsics::mul_with_overflow(left, right);
        let expected: (($ty, bool), ($ty, bool), ($ty, bool)) = ($add, $sub, $mul);
        assert_eq!((sum, difference, product), expected);
        let dynamic_left = std::hint::black_box(left);
        let dynamic_right = std::hint::black_box(right);
        assert_eq!(sum, dynamic_left.overflowing_add(dynamic_right));
        assert_eq!(difference, dynamic_left.overflowing_sub(dynamic_right));
        assert_eq!(product, dynamic_left.overflowing_mul(dynamic_right));
    }};
}
fn main() {
    check!(i8, 127, 1, (-128, true), (126, false), (127, false));
    check!(i8, -128, -1, (127, true), (-127, false), (-128, true));
    check!(i8, -128, 1, (-127, false), (127, true), (-128, false));
    check!(i8, 8, -16, (-8, false), (24, false), (-128, false));
    check!(i8, -8, -16, (-24, false), (8, false), (-128, true));
    check!(u8, 255, 1, (0, true), (254, false), (255, false));
    check!(u8, 0, 255, (255, false), (1, true), (0, false));
    check!(u8, 16, 16, (32, false), (0, false), (0, true));
    check!(i16, -32768, -1, (32767, true), (-32767, false), (-32768, true));
    check!(i32, i32::MAX, 1, (i32::MIN, true), (i32::MAX - 1, false), (i32::MAX, false));
    check!(i64, i64::MIN, 1, (i64::MIN + 1, false), (i64::MAX, true), (i64::MIN, false));
    check!(i128, i128::MIN, -1, (i128::MAX, true), (i128::MIN + 1, false), (i128::MIN, true));
    check!(u128, u128::MAX, 1, (0, true), (u128::MAX - 1, false), (u128::MAX, false));
    check!(u128, 1u128 << 127, 2, ((1u128 << 127) + 2, false), ((1u128 << 127) - 2, false), (0, true));
    check!(u128, u128::MAX, 0, (u128::MAX, false), (u128::MAX, false), (0, false));
    check!(isize, isize::MIN, -1, (isize::MAX, true), (isize::MIN + 1, false), (isize::MIN, true));
    check!(usize, usize::MAX, 1, (0, true), (usize::MAX - 1, false), (usize::MAX, false));
}
