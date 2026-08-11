
#![feature(lang_items)]

trait Foo {
    fn id(&self) -> i32;
}

struct Thing(i32);

impl Foo for Thing {
    fn id(&self) -> i32 {
        self.0
    }
}

fn make_thing() -> impl Foo {
    Thing(99)
}

fn gccrs_main() -> i32 {
    let v = make_thing();
    let r = &v;
    let val = r.id();
    val - 99
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
