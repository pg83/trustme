// { dg-additional-options "-w" }
#![allow(bindings_with_variant_name)]

enum E {
    A,
    B,
    C
}

fn gccrs_main() -> i32 {
    use E::C;

    let v1 = match E::A {
        C => 1,
        E::A => 0,
        E::B => 1
    };

    let v2 = match E::A {
        B => 0,
        E::A => 1,
        C => 1
    };

    v1 + v2
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
