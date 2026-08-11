// { dg-output "f\r*\nd\r*\n" }
// { dg-additional-options "-w" }
#![feature(lang_items)]

extern "C" {
    fn printf(s: *const i8, ...);
}

struct Droppable;

impl Drop for Droppable {
    fn drop(&mut self) {
        let msg = "d\n\0" as *const str as *const i8;
        unsafe {
            printf(msg);
        }
    }
}

fn foo() -> i32 {
    let msg = "f\n\0" as *const str as *const i8;
    unsafe {
        printf(msg);
    }

    0
}

fn f() -> i32 {
    let _x = Droppable;
    foo()
}

fn gccrs_main() -> i32 {
    f()
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
