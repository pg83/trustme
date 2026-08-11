// { dg-ice "" }
enum E {
    A,
    B,
}
const A: u32 = 0;
fn a(value: E) {
    let E::A = value else { return };
}
fn b(value: u32) {}
fn main() {
    let value: u32;
    let value: E;
}
