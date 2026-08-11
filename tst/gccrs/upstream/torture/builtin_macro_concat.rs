// { dg-output "\r*\ntest10btrue2.15\r*\ntest10bfalse2.151\r*\n" }

#![feature(rustc_attrs)]

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: &str) {
    println!("{}", s);
}

fn gccrs_main() -> i32 {
    let a = concat!();
    let b = concat!("test", 10, 'b', true, 2.15);
    let c = concat!("test", 10, 'b', false, 2.15, 1u64);
    print(a);
    print(b);
    print(c);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
