//@ compile-fail: error
fn main() {
    let _: Option<&dyn Send<u8>> = None;
}
