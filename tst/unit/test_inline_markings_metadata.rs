#[cfg(inline_markings_metadata)]
extern crate inline_markings_producer as producer;

#[cfg(not(inline_markings_metadata))]
mod producer {
    #[inline]
    pub fn trustme_inline_normal_probe(value: u32) -> u32 {
        value.wrapping_add(1)
    }

    #[inline(always)]
    pub fn trustme_inline_always_probe(value: u32) -> u32 {
        value.wrapping_mul(2)
    }
}

fn main() {
    assert_eq!(producer::trustme_inline_normal_probe(20), 21);
    assert_eq!(producer::trustme_inline_always_probe(21), 42);
}
