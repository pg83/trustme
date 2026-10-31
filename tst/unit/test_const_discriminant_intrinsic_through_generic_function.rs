#![feature(core_intrinsics, discriminant_kind)]
#[repr(i16)] enum Choice { Left=-13, Right=257 }
const fn tag<T>(value:&T) -> <T as std::marker::DiscriminantKind>::Discriminant { std::intrinsics::discriminant_value(value) }
const LEFT: i16 = tag(&Choice::Left);
const RIGHT: i16 = std::intrinsics::discriminant_value(&Choice::Right);
const ORDINARY: <u32 as std::marker::DiscriminantKind>::Discriminant = tag(&37u32);
fn main() { assert_eq!(LEFT,-13);assert_eq!(RIGHT,257);assert_eq!(ORDINARY,0); }
