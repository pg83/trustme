// { dg-additional-options "-w -frust-cfg=A" }
// { dg-output "A\r*\nB\r*\n" }

#![feature(rustc_attrs)]

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: &str) {
    println!("{}", s);
}

fn gccrs_main() -> i32 {
    let cfg = cfg!(A) || cfg!(B);
    if cfg {
        print("A");
    }
    let cfg = cfg!(A) && cfg!(B);
    if !cfg {
        print("B");
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
