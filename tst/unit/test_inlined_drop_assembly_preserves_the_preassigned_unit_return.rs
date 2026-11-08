//@ compile-flags: -O

struct Counter(*mut u32);

impl Drop for Counter {
    #[inline(always)]
    fn drop(&mut self) {
        unsafe {
            core::arch::asm!(
                "inc dword ptr [{counter}]",
                "/* {blocked} */",
                counter = in(reg) self.0,
                blocked = label { loop {} },
                options(nostack),
            );
        }
    }
}

fn main() {
    let mut count = 0u32;
    let guard = Counter(&mut count);
    drop(guard);
    assert_eq!(count, 1);
}
