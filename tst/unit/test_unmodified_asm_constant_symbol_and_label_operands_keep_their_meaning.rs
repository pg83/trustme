//@ compile-variant: -O
static SLOT: u32 = 487;
fn main() {
    let mut value = 19u32;
    unsafe { core::arch::asm!("add {0:e}, {bias}", inout(reg) value, bias = const 23, options(nomem, nostack)); }
    assert_eq!(value, 42);
    let address: *const u32;
    unsafe { core::arch::asm!("lea {address}, [rip + {symbol}]", address = out(reg) address, symbol = sym SLOT, options(nostack)); }
    assert_eq!(unsafe { *address }, 487);
    let mut seen = 0;
    for flag in [0usize, 1] {
        unsafe { core::arch::asm!("test {flag}, {flag}", "jz {zero}", flag = in(reg) flag, zero = label { seen += 1; }, options(nostack)); }
    }
    assert_eq!(seen, 1);
}
