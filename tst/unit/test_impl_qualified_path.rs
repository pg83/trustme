// `impl <Type>::Assoc {}` starts with `<`, but that is a qualified path naming
// the type being implemented, not a parameter list. A lifetime can only start a
// bare trait object, whose first bound is that lifetime.
//@ compile-flags: -Z parse-crate-root-only
//@ crate-type: lib

impl <*const u8>::AssocTy {}
impl <Type as Trait>::AssocTy {}
impl <'a + Trait>::AssocTy {}
impl <<Type>::AssocTy>::AssocTy {}

impl<T> Trait for T {}
impl<'a> Trait for &'a u8 {}
impl<const N: usize> Trait for [u8; N] {}
impl<T: Copy, U> Trait for (T, U) {}
