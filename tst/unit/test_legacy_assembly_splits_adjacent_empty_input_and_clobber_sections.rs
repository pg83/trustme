#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {
    () => {};
}

fn main() {
    let mut value = 13u32;
    unsafe {
        llvm_asm!("" : ::);
    }
    value += 7;
    assert_eq!(value, 20);
}
