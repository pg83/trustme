// x86_64's baseline CPU is x86-64, so the target's cfg lists the features
// it implies - `fxsr`, `sse`, `sse2` - as upstream's `--print cfg` does.
// Every `target_feature` read false, so code gated on
// `cfg(target_feature = "sse2")` - aho-corasick's Teddy searcher, which
// then picks ssse3 or avx2 at run time - was never built and its builder
// returned None.
fn main() {
    #[cfg(target_arch = "x86_64")]
    {
        assert!(cfg!(target_feature = "fxsr"));
        assert!(cfg!(target_feature = "sse"));
        assert!(cfg!(target_feature = "sse2"));
        assert!(!cfg!(target_feature = "avx2"));
        sse2_add();
    }
}

#[cfg(all(target_arch = "x86_64", target_feature = "sse2"))]
fn sse2_add() {
    use std::arch::x86_64::{__m128i, _mm_add_epi8, _mm_set1_epi8, _mm_storeu_si128};
    let mut out = [0u8; 16];
    unsafe {
        let three = _mm_set1_epi8(3);
        _mm_storeu_si128(out.as_mut_ptr() as *mut __m128i, _mm_add_epi8(three, three));
    }
    assert_eq!(out, [6u8; 16]);
}

#[cfg(all(target_arch = "x86_64", not(target_feature = "sse2")))]
fn sse2_add() {
    panic!("sse2 is not in the target's features");
}
