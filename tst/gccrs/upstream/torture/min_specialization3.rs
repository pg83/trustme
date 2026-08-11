
#![feature(min_specialization, lang_items)]

trait Foo {
    fn foo(&self) -> i32;
}

struct Wrap<T>(T);

impl<T> Foo for T {
    default fn foo(&self) -> i32 {
        15
    }
}

impl<T> Foo for Wrap<T> {
    default fn foo(&self) -> i32 {
        16
    }
}

impl Foo for Wrap<bool> {
    fn foo(&self) -> i32 {
        if self.0 {
            1
        } else {
            0
        }
    }
}

fn gccrs_main() -> i32 {
    Wrap(true).foo() - 1
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
