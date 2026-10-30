//@ compile-flags: -Zmir-opt-level=3
#![feature(core_intrinsics)]
#![allow(internal_features)]
#[inline]
fn count_choice(value:u32) -> u32 {
 match value^0x5a { 0=>std::intrinsics::ctlz(value), 1=>std::intrinsics::cttz(value), _=>std::intrinsics::ctpop(value) }
}
fn main() {
 assert_eq!(count_choice(0x5a),25);
 assert_eq!(count_choice(0x5b),0);
 assert_eq!(count_choice(std::hint::black_box(0x5e)),5);
}
