#![feature(yeet_expr)]
#![allow(unreachable_code)]

fn reject() -> Result<(), u8> {
    assert!(if do yeet 7u8 { true } else { false });
    Ok(())
}

fn main() {
    assert_eq!(reject(), Err(7));
}
