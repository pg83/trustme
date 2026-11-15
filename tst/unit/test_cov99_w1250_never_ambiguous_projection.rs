//@ compile-fail: type annotations needed

trait Choice {
    type Output;
}

impl Choice for u8 {
    type Output = ();
}

impl Choice for u16 {
    type Output = ();
}

fn main() {
    let _: <_ as Choice>::Output = loop {};
}
