//@ compile-flags: -O
#[inline(never)]
fn marker() -> u8 {
    73
}

#[inline(never)]
fn add_and_choose(mut value: usize) -> usize {
    unsafe {
        core::arch::asm!(
            "add {value}, {step}",
            value = inout(reg) value,
            step = const 11,
            options(nomem, nostack),
        );
        core::arch::asm!(
            "test {value}, {value}",
            "jz {zero}",
            value = in(reg) value,
            zero = label { return 99; },
            options(nomem, nostack),
        );
    }
    value
}

fn main() {
    let address: usize;
    unsafe {
        core::arch::asm!(
            "lea {address}, [rip + {function}]",
            address = out(reg) address,
            function = sym marker,
            options(nomem, nostack, preserves_flags),
        );
    }
    assert_eq!(address, marker as usize);
    assert_eq!(marker(), 73);
    assert_eq!(add_and_choose(std::hint::black_box(9)), 20);
    assert_eq!(add_and_choose(std::hint::black_box(usize::MAX - 10)), 99);
}
