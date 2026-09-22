//@ aux-build: legacy_const_shift.rs
//@ crate-type: lib
// A `#[rustc_legacy_const_generics]` argument becomes a const argument of
// the call - `shift_right(a, BITS)` is `shift_right::<{ BITS }>(a)` - and
// inside a generic method that constant depends on the method's parameter,
// as upstream's anon const takes its parent's generic args. The check for
// "this path still needs monomorphising" looked only at bare generic values,
// so the library's public items were enumerated with the constant as if it
// were concrete and the compiler aborted ("Value param ... out of range");
// aho-corasick's AVX2 Teddy calls `_mm256_srli_epi16(self, BITS)` so.
extern crate legacy_const_shift;

pub struct Lanes(pub u32);

pub trait Shift {
    fn shift<const BITS: i32>(self) -> Self;
    fn by_four(self) -> Self;
}

impl Shift for Lanes {
    #[inline(always)]
    fn shift<const BITS: i32>(self) -> Self {
        Lanes(legacy_const_shift::shift_right(self.0, BITS))
    }

    fn by_four(self) -> Self {
        self.shift::<4>()
    }
}

pub fn sixteen() -> u32 {
    Lanes(256).by_four().0
}
