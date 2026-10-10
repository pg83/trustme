// x86-64 aligns i128 and u128 to 16 bytes since Rust 1.77 (LLVM's data layout
// gained `i128:128`), matching C's `__int128`. We emulated 128-bit integers as
// a pair of `u64` on x86-64 and gave them the alignment of `u64`, so a
// `#[repr(C)]` struct holding one had another layout than its C counterpart,
// and fastbloom's hashing ran a 128-step shift-and-add for every multiply.
#[repr(C)]
struct Tagged {
    tag: u8,
    value: u128,
}

fn mix(a: u64, b: u64) -> u64 {
    let full = (a as u128).wrapping_mul(b as u128);
    (full as u64) ^ ((full >> 64) as u64)
}

fn main() {
    assert_eq!(std::mem::align_of::<u128>(), 16);
    assert_eq!(std::mem::align_of::<i128>(), 16);
    assert_eq!(std::mem::size_of::<Tagged>(), 32);
    assert_eq!(std::mem::offset_of!(Tagged, value), 16);
    assert_eq!(std::mem::size_of::<(u8, i128)>(), 32);
    let t = Tagged { tag: 7, value: u128::MAX - 1 };
    assert_eq!(t.tag, 7);
    assert_eq!(t.value.wrapping_add(1), u128::MAX);

    let x: u128 = 0xffff_ffff_ffff_ffff_1234;
    assert_eq!(x.wrapping_mul(0x9e37_79b9_7f4a_7c15), 0x79b97f4a7c146d08901a45569bc5ae44);
    assert_eq!(x.checked_mul(u128::MAX / 3), None);
    assert_eq!(u128::MAX.overflowing_add(2), (1, true));
    assert_eq!(i128::MIN.overflowing_sub(1), (i128::MAX, true));
    assert_eq!((-7i128).checked_div(2), Some(-3));
    assert_eq!((-7i128) % 2, -1);
    assert_eq!(i128::MIN.checked_div(-1), None);
    assert_eq!((1u128 << 100) / 3, 422550200076076467165567735125);
    assert_eq!(mix(0x9e37_79b9_7f4a_7c15, 0xbf58_476d_1ce4_e5b9), 0xa035_e2cc_637f_5704);
    assert!((1i128 << 90) > (1i128 << 89));
    assert!(-1i128 < 0);
    assert_eq!(u128::MAX.leading_zeros(), 0);
    assert_eq!(1u128.trailing_zeros(), 0);
    assert_eq!((u128::MAX as f64) as u128, u128::MAX);
    assert_eq!(format!("{}", i128::MIN), "-170141183460469231731687303715884105728");
}
