
#![feature(lang_items)]

trait Magic {
    fn magic(&self) -> usize;
}

struct Foo<const N: usize>;

impl<const N: usize> Magic for Foo<N> {
    fn magic(&self) -> usize {
        N
    }
}

fn gccrs_main() -> i32 {
    let f = Foo::<7> {};
    let n = f.magic();
    n as i32 - 7
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
