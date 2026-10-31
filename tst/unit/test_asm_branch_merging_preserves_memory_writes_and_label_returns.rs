#[inline(never)]
fn select(mode: u8, output: &mut u32) -> u32 {
    let address = output as *mut u32;
    unsafe {
        match mode {
            0 => std::arch::asm!("mov dword ptr [{address}], 37", "jmp {exit}", address = in(reg) address, exit = label { return *output; }, options(noreturn, nostack)),
            1 => std::arch::asm!("mov dword ptr [{address}], 37", "jmp {exit}", address = in(reg) address, exit = label { return *output; }, options(noreturn, nostack)),
            _ => std::arch::asm!("mov dword ptr [{address}], 41", "jmp {exit}", address = in(reg) address, exit = label { return *output; }, options(noreturn, nostack)),
        }
    }
}
fn main() {
    for (mode, expected) in [(0, 37), (1, 37), (2, 41)] {
        let mut output = 0;
        assert_eq!(select(std::hint::black_box(mode), &mut output), expected);
        assert_eq!(output, expected);
    }
}
