
#![feature(lang_items)]

struct Foo<const N: usize>;

impl<const N: usize> Foo<N> {
    const VALUE: usize = N;
    const SQUARE: usize = N * N;
}

fn gccrs_main() -> i32 {
    let a = Foo::<5>::VALUE; // 5
    let b = Foo::<5>::SQUARE; // 25
    (a + b) as i32 - 30
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
