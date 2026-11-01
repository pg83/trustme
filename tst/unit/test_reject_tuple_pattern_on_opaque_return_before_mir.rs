//@ compile-fail: error:0:

fn make() -> impl Copy { (17u8, 257u16) }

fn main() {
    let (first, last) = make();
    let _ = (first, last);
}
