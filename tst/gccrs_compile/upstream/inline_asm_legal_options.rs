
#![feature(rustc_attrs)]

fn main() {
    unsafe {
        std::arch::asm!("nop", options(nomem, nostack, att_syntax, raw));
    }
}