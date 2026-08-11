
#![feature(rustc_attrs)]

fn main() {
    unsafe {
        std::arch::asm!("nop");
        std::arch::asm!("nop",);
    }
}
