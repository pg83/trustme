
#![feature(min_specialization, lang_items)]

trait Foo {
    fn foo(&self) -> i32;
}

impl<T> Foo for T {
    default fn foo(&self) -> i32 {
        15
    }
}

impl Foo for bool {
    fn foo(&self) -> i32 {
        if *self {
            1
        } else {
            0
        }
    }
}

fn gccrs_main() -> i32 {
    let a = 1.foo() - 15;
    let b = true.foo() - 1;

    a + b
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
