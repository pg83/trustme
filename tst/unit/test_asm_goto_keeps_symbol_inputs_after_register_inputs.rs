//@ compile-variant: -O
#![feature(asm_goto_with_outputs)]
fn target() -> usize { 47 }

fn choose(flag: u32) -> usize {
    let address: usize;
    let mut bonus = 0;
    unsafe {
        core::arch::asm!("lea {address}, [rip + {function}]", "cmp {flag:e}, {selected}", "je {done}",
                         address = out(reg) address, flag = in(reg) flag,
                         function = sym target, selected = const 7,
                         done = label { bonus = 3; }, options(nostack));
        let call: fn() -> usize = std::mem::transmute(address);
        call() + bonus
    }
}

fn main() {
    for flag in [0, 7, 19] {
        assert_eq!(choose(std::hint::black_box(flag)), if flag == 7 { 50 } else { 47 });
    }
}
