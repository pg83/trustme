//@ proc-macro-aux-build: late_module_tokens.rs
#![feature(pattern_types, pattern_type_macro)]
#[late_module_tokens::keep_tree]
mod orchard {
    pub fn selected() -> u32 {
        let value: std::pat::pattern_type!(u32 is 3..=9) = 7;
        unsafe { std::mem::transmute(value) }
    }
}
fn main() { assert_eq!(orchard::selected(), 7); }
