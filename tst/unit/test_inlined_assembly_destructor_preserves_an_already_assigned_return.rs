//@ compile-flags: -O
#![feature(asm_goto_with_outputs)]

struct Tick {
    flag: u32,
    counter: *mut u32,
}

impl Drop for Tick {
    #[inline(always)]
    fn drop(&mut self) {
        unsafe {
            core::arch::asm!(
                "test {flag:e}, {flag:e}",
                "jnz {alternate}",
                "inc dword ptr [{counter}]",
                flag = in(reg) self.flag,
                counter = in(reg) self.counter,
                alternate = label {
                    *self.counter += 5;
                    return;
                },
                options(nostack),
            );
        }
    }
}

#[inline(never)]
fn finish(flag: u32, counter: &mut u32) -> u64 {
    let _tick = Tick { flag, counter };
    37
}

fn main() {
    let mut counter = 0;
    assert_eq!(finish(core::hint::black_box(0), &mut counter), 37);
    assert_eq!(counter, 1);
    assert_eq!(finish(core::hint::black_box(1), &mut counter), 37);
    assert_eq!(counter, 6);
}
