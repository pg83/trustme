//@ compile-fail: invalid asm template modifier
static SLOT: u16 = 401;
fn main() {
    unsafe { core::arch::asm!("lea rax, [rip + {item:r}]", item = sym SLOT, out("rax") _, options(nostack)); }
}
