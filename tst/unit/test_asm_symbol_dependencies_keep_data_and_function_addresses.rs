//@ compile-variant: -O
static DATA: u64 = 37;
extern "C" fn target() -> u64 { 41 }
fn main() {
    let data: *const u64;
    let function: *const ();
    unsafe {
        std::arch::asm!("lea {out}, [rip + {symbol}]", out = out(reg) data, symbol = sym DATA, options(nomem, nostack, preserves_flags));
        std::arch::asm!("lea {out}, [rip + {symbol}]", out = out(reg) function, symbol = sym target, options(nomem, nostack, preserves_flags));
        let call: extern "C" fn() -> u64 = std::mem::transmute(function);
        assert_eq!(*data + call(), 78);
    }
}
