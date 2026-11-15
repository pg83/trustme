//@ compile-flags: -C panic=abort
#![feature(naked_functions_rustic_abi)]

trait Tracked {
    #[track_caller]
    unsafe fn ping();
}

struct Leaf;

impl Tracked for Leaf {
    #[unsafe(naked)]
    unsafe fn ping() {
        std::arch::naked_asm!("ret");
    }
}

fn main() {
    let ping: unsafe fn() = Leaf::ping;
    unsafe { ping() };
}
