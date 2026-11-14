//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(intrinsics, rustc_attrs)]

#[rustc_intrinsic]
const unsafe fn rotate_left<T: ?Sized>(_value: u128, _count: u128);

const CHECK: () = unsafe { rotate_left::<str>(1u128, 18446744073709551616u128) };

fn main() {
    let () = CHECK;
}
