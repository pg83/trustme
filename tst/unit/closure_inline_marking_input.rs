/* The same program twice, differing only in the `#[inline(always)]` written on
   the closure - the shape zerocopy and aho-corasick pass to `map_err`, `try_with`
   and `for_each_64bit_lane`. */

fn apply<F: FnOnce(u32) -> u32>(f: F, value: u32) -> u32 {
    f(value)
}

#[cfg(not(closure_inline_marking))]
#[no_mangle]
pub fn trustme_closure_inline_probe(value: u32) -> u32 {
    apply(|x: u32| x.wrapping_mul(3), value)
}

#[cfg(closure_inline_marking)]
#[no_mangle]
pub fn trustme_closure_inline_probe(value: u32) -> u32 {
    apply(
        #[inline(always)]
        |x: u32| x.wrapping_mul(3),
        value,
    )
}

fn main() {
    assert_eq!(trustme_closure_inline_probe(14), 42);
}
