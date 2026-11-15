//@ compile-fail: cannot match

struct Marker;

fn main() {
    let Marker: u8 = 1;
}
