// { dg-output "d\r*\nd\r*\n" }
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

fn foo() {}

fn unit_tail_call() {
    let _x = Droppable;
    foo()
}

fn unit_tail_literal() {
    let _x = Droppable;
    ()
}

fn gccrs_main() -> i32 {
    unit_tail_call();
    unit_tail_literal();
    0
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
