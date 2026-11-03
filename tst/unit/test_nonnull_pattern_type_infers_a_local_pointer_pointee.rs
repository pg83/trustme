#![feature(pattern_types, pattern_type_macro)]
fn main() {
    let byte = 199u8;
    let known: std::pat::pattern_type!(*const u8 is !null) = unsafe { std::mem::transmute(&byte as *const u8) };
    let inferred: std::pat::pattern_type!(*const _ is !null) = known;
    let raw: *const u8 = unsafe { std::mem::transmute(inferred) };
    assert_eq!(unsafe { *raw }, 199);
}
