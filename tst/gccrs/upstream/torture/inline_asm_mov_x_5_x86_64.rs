/* { dg-do run { target x86_64*-*-* } } */
/* { dg-output "5\r*\n9\r*\n" }*/


#![feature(rustc_attrs)]
extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    let mut x: i32 = 0;
    let mut _y: i32 = 9; // Mark it as _y since it is only used as input operand, not printing

    unsafe {
        std::arch::asm!(
            "mov {}, 5",
            out(reg) x
        );
        println!("{}", x);
    };

    unsafe {
        std::arch::asm!(
            "mov {}, {}",
            in(reg) _y,
            out(reg) x,
        );
        println!("{}", x);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
