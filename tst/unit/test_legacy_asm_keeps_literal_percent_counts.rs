//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $constraint:literal($dst:expr) : : : $($flag:literal),+) => {{
        core::arch::asm!($text, out("eax") $dst, options(nostack));
    }};
}
fn main() {
    let result: u32;
    unsafe {
        llvm_asm!(".ifc \"%%\",\"%\"\nmov eax, 0\n.else\nmov eax, 211\n.endif\n"
            : "={eax}"(result) : : : "volatile", "intel");
    }
    assert_eq!(result, 211);
}
