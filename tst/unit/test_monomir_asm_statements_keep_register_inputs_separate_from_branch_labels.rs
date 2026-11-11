//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zdump-mir -O
pub static SENTINEL: usize = 137;

pub fn select(input: usize) -> usize {
    let changed: usize;
    let address: *const usize;
    unsafe {
        core::arch::asm!(
            "mov {changed}, {input}",
            "xor {changed}, {mask}",
            changed = lateout(reg) changed,
            input = in(reg) input,
            mask = const 61,
            options(nomem, nostack),
        );
        core::arch::asm!(
            "lea {address}, [rip + {sentinel}]",
            address = out(reg) address,
            sentinel = sym SENTINEL,
            options(nomem, nostack, preserves_flags),
        );
        let sentinel = *address;
        core::arch::asm!(
            "test {changed}, {changed}",
            "jz {same}",
            changed = in(reg) changed,
            same = label { return sentinel; },
            options(nomem, nostack),
        );
        changed.wrapping_add(sentinel)
    }
}
