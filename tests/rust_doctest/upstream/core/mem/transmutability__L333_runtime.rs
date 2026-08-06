// Extracted from library/core/src/mem/transmutability.rs:333
#![allow(unused)]
#![feature(
#![allow(incomplete_features)]
fn main() {
        adt_const_params,
        generic_const_exprs,
        pointer_is_aligned_to,
        transmutability,
    )]
    use core::mem::{Assume, TransmuteFrom};
    
    /// Attempts to transmute `src` to `&Dst`.
    ///
    /// Returns `None` if `src` violates the alignment requirements of `&Dst`.
    ///
    /// # Safety
    ///
    /// The caller guarantees that the obligations required by `ASSUME`, except
    /// alignment, are satisfied.
    unsafe fn try_transmute_ref<'a, Src, Dst, const ASSUME: Assume>(src: &'a Src) -> Option<&'a Dst>
    where
        &'a Dst: TransmuteFrom<&'a Src, { ASSUME.and(Assume::ALIGNMENT) }>,
    {
        if <*const _>::is_aligned_to(src, align_of::<Dst>()) {
            // SAFETY: By the above dynamic check, we have ensured that the address
            // of `src` satisfies the alignment requirements of `&Dst`. By contract
            // on the caller, the safety obligations required by `ASSUME` have also
            // been satisfied.
            Some(unsafe {
                <_ as TransmuteFrom<_, { ASSUME.and(Assume::ALIGNMENT) }>>::transmute(src)
            })
        } else {
            None
        }
    }
    
    let src: &[u8; 2] = &[0xFF, 0xFF];
    
    // SAFETY: No safety obligations.
    let maybe_dst: Option<&u16> = unsafe {
        try_transmute_ref::<_, _, { Assume::NOTHING }>(src)
    };
}
