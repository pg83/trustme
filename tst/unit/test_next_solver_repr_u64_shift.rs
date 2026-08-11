//@ check-pass
//@ compile-flags: -Znext-solver

#[repr(u64)]
enum Alignment {
    One = 1 << 0,
    Two = 1 << 1,
}

fn main() {}
