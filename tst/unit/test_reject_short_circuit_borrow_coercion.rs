//@ compile-fail: Type mismatch
fn invalid() {
    let _: &usize = &(false && panic!("not evaluated"));
}
fn main() {}
