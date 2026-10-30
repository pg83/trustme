#![feature(core_intrinsics)]
#![allow(internal_features)]
use std::num::NonZeroU64;
enum EmptyFirst { Empty, Full(NonZeroU64) }
enum EmptyLast { Full(NonZeroU64), Empty }
const NONZERO:NonZeroU64=unsafe { NonZeroU64::new_unchecked(0x1200000000) };
const TAGS:[u64;4]=[
 std::intrinsics::discriminant_value(&EmptyFirst::Empty) as u64,
 std::intrinsics::discriminant_value(&EmptyFirst::Full(NONZERO)) as u64,
 std::intrinsics::discriminant_value(&EmptyLast::Full(NONZERO)) as u64,
 std::intrinsics::discriminant_value(&EmptyLast::Empty) as u64,
];
fn main() { assert_eq!(TAGS,[0,1,0,1]); assert_eq!(std::mem::size_of::<EmptyFirst>(),8); assert_eq!(std::mem::size_of::<EmptyLast>(),8); }
