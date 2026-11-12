//@ compile-fail: captures

fn convert(value: u16) {
    let _ = (move || value) as fn() -> u16;
}

fn main() {
    convert(181);
}
