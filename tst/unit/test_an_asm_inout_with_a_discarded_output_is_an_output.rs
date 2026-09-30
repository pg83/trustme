//@ compile-flags: -C opt-level=2
// cmov's constant-time equality (under crypto-bigint's `Limb: PartialEq`)
// XORs its left operand in place: `inout(reg) *lhs => _`. The register is
// written, so the operand is an output nobody reads, tied to its input -
// upstream hands LLVM an output constraint for it. Emitted as a plain input,
// the C compiler took the register to be left intact and read the XOR's
// result where it kept the operand's value afterwards.
use std::arch::asm;

#[inline(never)]
fn class_register(a: u64, b: u64) -> (u64, u16) {
    let mut tmp: u16 = 0;
    let condition: u16 = 1;
    unsafe {
        asm!(
            "xor {0}, {1}",
            "cmovz {2:e}, {3:e}",
            inout(reg) a => _,
            in(reg) b,
            inlateout(reg) tmp,
            in(reg) condition,
            options(pure, nomem, nostack),
        );
    }
    (a, tmp)
}

#[inline(never)]
fn explicit_register(a: u64, b: u64) -> u64 {
    unsafe {
        asm!("xor rcx, {0}", in(reg) b, inout("rcx") a => _, options(nomem, nostack));
    }
    a
}

fn main() {
    let (five, seven) = (5u64, 7u64);
    let a = unsafe { std::ptr::read_volatile(&five) };
    let b = unsafe { std::ptr::read_volatile(&seven) };
    assert_eq!(class_register(a, b), (5, 0));
    assert_eq!(class_register(a, a), (5, 1));
    assert_eq!(explicit_register(a, b), 5);
}
