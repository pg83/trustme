//@ compile-variant: -O
static MARKER: u8 = 41;

fn adjust(mut value: u32) -> u32 {
    unsafe {
        core::arch::asm!("add {value:e}, {step}", value = inout(reg) value,
                         step = const 7, options(nomem, nostack));
    }
    value
}

fn main() {
    assert_eq!(adjust(std::hint::black_box(11)), 18);
    let mut values = [3u32, 5];
    unsafe {
        core::arch::asm!("mov {output:e}, {replacement}",
                         output = lateout(reg) values[std::hint::black_box(1)],
                         replacement = const 13, options(nomem, nostack, preserves_flags));
    }
    assert_eq!(values, [3, 13]);
    let address: usize;
    unsafe {
        core::arch::asm!("lea {address}, [rip + {symbol}]", address = out(reg) address,
                         symbol = sym MARKER, options(nomem, nostack, preserves_flags));
        assert_eq!(address, &MARKER as *const u8 as usize);
        assert_eq!(*(address as *const u8), 41);
    }
}
