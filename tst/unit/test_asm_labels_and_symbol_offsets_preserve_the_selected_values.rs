static DATA: [u8; 3] = [17, 23, 41];
fn choose(flag: usize) -> u32 {
    let mut value = 17;
    unsafe {
        std::arch::asm!("test {flag}, {flag}", "jnz {exit}", flag = in(reg) flag, exit = label { value = 37; }, options(nomem, nostack));
    }
    value
}
fn main() {
    assert_eq!((choose(0), choose(1)), (17, 37));
    let address: *const u8;
    unsafe {
        std::arch::asm!("lea {address}, [rip + {data} + {offset}]", address = out(reg) address, data = sym DATA, offset = const 1, options(nomem, nostack, preserves_flags));
        assert_eq!(*address, 23);
    }
}
