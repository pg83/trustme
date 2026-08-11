/* { dg-do run { target x86_64*-*-* } } */
/* { dg-output "Value is: 5\r*\n" } */

#![feature(rustc_attrs)]

extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    let y: i32 = 4;
    let x: i32;
    // `inout` can also move values to different places
    unsafe {
        std::arch::asm!("inc {}", inout(reg) y=>x);
    }
    unsafe {
        printf("Value is: %i\n\0" as *const str as *const i8, x);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
