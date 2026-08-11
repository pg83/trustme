
#![feature(rustc_attrs)]

fn main() {
    unsafe {
        std::arch::asm!("nop");
    }
}