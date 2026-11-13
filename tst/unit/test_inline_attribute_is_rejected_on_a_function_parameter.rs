//@ compile-fail: error:0: attribute is not allowed in this position

fn read(#[inline] value: u16) -> u16 {
    value
}

fn main() {
    let _ = read(659);
}
