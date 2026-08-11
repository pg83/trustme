/* { dg-do run { target x86_64*-*-* } } */
/* { dg-output "Value is: 5\r*\n" } */

#![feature(rustc_attrs)]

extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    let x: u64;
    // `inout` can also move values to different places
    unsafe {
        std::arch::asm!("inc {}", inout(reg) 4u64=>x);
    }
    unsafe {
        printf("Value is: %lu\n\0" as *const str as *const i8, x);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
