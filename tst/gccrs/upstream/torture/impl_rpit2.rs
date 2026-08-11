
#![feature(lang_items)]

trait Foo {
    fn id(&self) -> i32;
}

struct Thing(i32);

impl Thing {
    fn double(&self) -> i32 {
        // { dg-warning "associated function is never used: .double." "" { target *-*-* } .-1 }
        self.0 * 2
    }
}

impl Foo for Thing {
    fn id(&self) -> i32 {
        self.0
    }
}

fn make_thing(a: i32) -> impl Foo {
    Thing(a)
}

fn use_foo(f: impl Foo) -> i32 {
    f.id()
}

fn gccrs_main() -> i32 {
    let value = make_thing(21);
    let id = use_foo(value);

    id - 21
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
