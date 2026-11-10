//@ compile-flags: -Zmir-opt-level=0
#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {
    () => {};
}

fn update(value: &mut u32) {
    unsafe { llvm_asm!(""); }
    *value += 7;
}

fn main() {
    let mut value = 31;
    update(&mut value);
    assert_eq!(value, 38);
}
