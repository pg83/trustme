/* { dg-output "called Foo::print\\(\\)\r*" } */
/* { dg-options "-w" } */

#![feature(lang_items)]

trait Printable {
    fn print(&self);
}

struct Foo;

impl Printable for Foo {
    fn print(&self) {
        // Simulate output
        unsafe {
            puts("called Foo::print()\0" as *const _ as *const i8);
        }
    }
}

fn get_printable() -> impl Printable {
    Foo
}

extern "C" {
    fn puts(s: *const i8);
}

fn gccrs_main() -> i32 {
    let p = get_printable();
    p.print();

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
