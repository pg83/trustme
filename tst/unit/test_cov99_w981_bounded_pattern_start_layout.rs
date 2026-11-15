//@ compile-flags: -Z dump-mir
#![feature(core_intrinsics, pattern_types, pattern_type_macro, generic_pattern_types, generic_const_exprs)]

trait LowerBound {
    const START: u8;
}
struct Two;
impl LowerBound for Two {
    const START: u8 = 2;
}

fn tuple_width<T: LowerBound>() -> usize
where
    std::pat::pattern_type!(u8 is T::START..=9): Sized,
{
    std::intrinsics::size_of::<(std::pat::pattern_type!(u8 is T::START..=9),)>()
}

fn main() {
    assert_eq!(tuple_width::<Two>(), 1);
}
