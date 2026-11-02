//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]
macro_rules! twice { ($body:expr) => {{ let first = $body; assert!($body); first }}; }
fn main() {
    assert!(twice!({
        type Present = std::pat::pattern_type!(*const u8 is !null);
        let byte = 53u8;
        let value: Present = unsafe { std::mem::transmute(&byte as *const u8) };
        unsafe { *std::mem::transmute::<Present, *const u8>(value) == 53 }
    }));
}
