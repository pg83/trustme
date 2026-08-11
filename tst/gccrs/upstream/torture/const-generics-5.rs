
#![feature(lang_items)]

struct Foo<const N: usize>;

impl<const N: usize> Foo<N> {
    const VALUE: usize = N;
}

fn gccrs_main() -> i32 {
    let val = Foo::<7>::VALUE;
    val as i32 - 7
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
