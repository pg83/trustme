//@ compile-flags: -O
#![feature(core_intrinsics)]

struct Counter(*mut u32);

const fn constant_mode(_: *mut u32) -> u8 { 2 }
fn runtime_mode(_: *mut u32) -> u8 { 0 }
const fn constant(_: u8, _: *mut u32) {}

fn runtime(mode: u8, counter: *mut u32) {
    unsafe {
        match mode {
            0 => core::arch::asm!(
                "inc dword ptr [{counter}]",
                "/* {blocked} */",
                counter = in(reg) counter,
                blocked = label { loop {} },
                options(nostack),
            ),
            1 => core::arch::asm!(
                "add dword ptr [{counter}], 2",
                counter = in(reg) counter,
                options(nostack),
            ),
            _ => core::arch::asm!(
                "add dword ptr [{counter}], 4",
                counter = in(reg) counter,
                options(nostack),
            ),
        }
    }
}

impl Drop for Counter {
    fn drop(&mut self) {
        unsafe {
            let mode = core::intrinsics::const_eval_select((self.0,), constant_mode, runtime_mode);
            core::intrinsics::const_eval_select((mode, self.0), constant, runtime);
        }
    }
}

fn main() {
    let mut count = 0u32;
    drop(Counter(&mut count));
    assert_eq!(count, 1);
}
