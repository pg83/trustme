
#![feature(rustc_attrs)]

fn main() {
    let mut _num1: i32 = 10;
    let mut _num2: i32 = 10;
    unsafe {
        std::arch::asm!(
            "mov {}, 4; mov {}, 4",
            out(reg) _num1,
            out(reg) _num2,
        );
    }
}
