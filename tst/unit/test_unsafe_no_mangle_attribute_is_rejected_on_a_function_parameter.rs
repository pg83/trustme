//@ compile-fail: error:0: attribute is not allowed on function parameters

fn read(#[unsafe(no_mangle)] value: u16) -> u16 {
    value
}

fn main() {
    let _ = read(673);
}
