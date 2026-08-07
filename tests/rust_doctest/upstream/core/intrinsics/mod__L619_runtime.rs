// Extracted from library/core/src/intrinsics/mod.rs:619
#![allow(unused)]
fn main() {
    struct R<'a>(&'a i32);
    unsafe fn extend_lifetime<'b>(r: R<'b>) -> R<'static> {
        unsafe { std::mem::transmute::<R<'b>, R<'static>>(r) }
    }

    unsafe fn shorten_invariant_lifetime<'b, 'c>(r: &'b mut R<'static>)
                                                 -> &'b mut R<'c> {
        unsafe { std::mem::transmute::<&'b mut R<'static>, &'b mut R<'c>>(r) }
    }
}
