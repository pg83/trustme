//@ proc-macro-aux-build: late_module_tokens.rs
#![feature(pattern_types, pattern_type_macro)]
#[late_module_tokens::keep_tree]
mod forest {
    pub fn selected(value: &u8) -> u8 {
        let pointer: std::pat::pattern_type!(*const u8 is !null) = unsafe {
            std::mem::transmute(value as *const u8)
        };
        let raw: *const u8 = unsafe { std::mem::transmute(pointer) };
        unsafe { *raw }
    }
}
fn main() { assert_eq!(forest::selected(&109), 109); }
