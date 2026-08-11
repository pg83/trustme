// { dg-output "d\r*\nd\r*\nd\r*\n" }
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

fn gccrs_main() -> i32 {
    {
        let _x = Droppable;
    }
    {
        let x = Droppable;
    }
    {
        let mut x = Droppable;
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
