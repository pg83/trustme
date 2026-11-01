//@ compile-fail: type annotations needed
fn main() {
    let _ = |callable| { let _: u8 = callable(); };
}
