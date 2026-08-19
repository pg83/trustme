//@ run-pass
// GCC spells the top of the x87 stack `st`, where Rust spells it `st(0)`; the
// rest of the stack agrees. A program that clobbers the whole stack names all
// eight, and the first of them has to be translated on the way out.

#![cfg(target_arch = "x86_64")]

fn add_through_x87(x: f64, y: f64) -> f64 {
    let mut out = 0f64;
    let mut status = 0u16;
    unsafe {
        core::arch::asm!(
            "fld qword ptr [{x}]",
            "fld qword ptr [{y}]",
            "faddp",
            "fstp qword ptr [{out}]",
            "xor eax, eax",
            "fstsw ax",
            "shl eax, 11",
            x = in(reg) &x,
            y = in(reg) &y,
            out = in(reg) &mut out,
            out("st(0)") _, out("st(1)") _, out("st(2)") _, out("st(3)") _,
            out("st(4)") _, out("st(5)") _, out("st(6)") _, out("st(7)") _,
            out("eax") status,
        );
    }
    assert_eq!(status & 0x7, 0);
    out
}

fn main() {
    assert_eq!(add_through_x87(1.0, 1.0), 2.0);
    assert_eq!(add_through_x87(0.5, 0.25), 0.75);
}
