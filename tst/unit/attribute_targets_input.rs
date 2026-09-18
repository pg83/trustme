#![feature(no_core)]
#![no_core]
#![crate_type = "lib"]

/* Attribute positions rustc 1.90 accepts without a word. Each one used to draw
   a "Unexpected attribute .. on .." warning out of the expansion pass; see
   test_attribute_targets.py for the upstream rule each models. */

pub struct S;

impl S {
    /* `check_target_feature`: Target::Method(MethodKind::Inherent) */
    #[target_feature(enable = "sse2", enable = "avx2")]
    pub unsafe fn inherent(&self) {}
}

pub trait T {
    /* `check_target_feature`: Target::Method(MethodKind::Trait { body: true }) */
    #[target_feature(enable = "avx2")]
    unsafe fn provided(&self) {}
}

pub fn takes<F>(_f: F) {}

pub fn closure_inline() {
    /* `check_inline`: Target::Closure */
    takes(#[inline(always)] |x: u32| x);
    takes(#[inline] |x: u32| x);
    takes(#[inline(never)] |x: u32| x);
}
