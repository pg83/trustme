//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir
//@ output-contains: .mir fn main#(isize, *const *const i8): isize {
//@ output-contains: .mir (arg0, arg1) goto 1 else 1;

#[start]
fn custom_start(_argc: isize, _argv: *const *const u8) -> isize {
    0
}

fn main() {}
