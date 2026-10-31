#![feature(core_intrinsics)]
const VALUES: (i32, u32, i32, i32, i32) = unsafe {
    (
        std::intrinsics::unchecked_mul(-7i32, 3),
        std::intrinsics::unchecked_shl(5u32, 3),
        std::intrinsics::unchecked_rem(-23i32, 5),
        std::intrinsics::unchecked_div(-23i32, 5),
        std::intrinsics::exact_div(-21i32, 3),
    )
};
fn main() { assert_eq!(VALUES, (-21, 40, -3, -4, -7)); }
