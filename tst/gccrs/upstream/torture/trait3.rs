/* { dg-output "123, 777\r*" } */

#![feature(lang_items)]

extern "C" {
    fn printf(s: *const i8, ...);
}

trait A {
    fn a() -> i32 {
        123
    }
}

trait B: A {
    fn b() -> i32 {
        <T as A>::a() + 456
    }
}

struct T;
// { dg-warning "struct is never constructed" "" { target *-*-* } .-1 }

impl A for T {
    fn a() -> i32 {
        321
    }
}

struct S;
impl A for S {}
impl B for S {}

fn gccrs_main() -> i32 {
    let aa = S::a();
    let bb = S::b();

    unsafe {
        let a = "%i, %i\n\0";
        let b = a as *const str;
        let c = b as *const i8;

        printf(c, aa, bb);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
