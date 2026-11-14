//@ compile-only
//@ compile-flags: --emit=metadata

trait Project {
    type Output;
}

impl Project for fn(u8) {
    type Output = u8;
}

fn main() {
    let _: <fn(_) as Project>::Output = 7_u8;
}
