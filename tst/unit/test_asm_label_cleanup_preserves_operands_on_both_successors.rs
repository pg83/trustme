//@ compile-variant: -O
#![feature(asm_goto_with_outputs)]
static ANCHOR: u8 = 47;

fn input_only(flag: u32) -> u32 {
    let mut value = 11;
    unsafe {
        core::arch::asm!("cmp {flag:e}, {selected}", "je {target}",
                         flag = in(reg) flag, selected = const 7,
                         target = label { value = 29; }, options(nostack));
    }
    value
}

fn with_output(flag: u32) -> u32 {
    let value: u32;
    let address: usize;
    let mut bonus = 0;
    unsafe {
        core::arch::asm!("mov {value:e}, {base}", "lea {address}, [rip + {anchor}]",
                         "cmp {flag:e}, {selected}", "je {target}",
                         value = out(reg) value, address = out(reg) address, flag = in(reg) flag,
                         base = const 31, selected = const 7,
                         anchor = sym ANCHOR,
                         target = label { bonus = value + 1; }, options(nostack));
        assert_eq!(*(address as *const u8), 47);
    }
    value + bonus
}

fn main() {
    for flag in [0, 7, 19] {
        let flag = std::hint::black_box(flag);
        assert_eq!(input_only(flag), if flag == 7 { 29 } else { 11 });
        assert_eq!(with_output(flag), if flag == 7 { 63 } else { 31 });
    }
}
