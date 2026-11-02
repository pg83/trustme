//@ compile-variant: -O

#[inline(never)]
fn route(mode: u8) -> u32 {
    unsafe {
        match mode {
            0 => std::arch::asm!("jmp {done}", done = label { return 23; }, options(nostack)),
            1 => std::arch::asm!("jmp {done}", done = label { return 23; }, options(nostack)),
            _ => std::arch::asm!("jmp {done}", done = label { return 23; }),
        }
    }
    29
}

fn main() {
    for mode in 0..3 { assert_eq!(route(std::hint::black_box(mode)), 23); }
}
