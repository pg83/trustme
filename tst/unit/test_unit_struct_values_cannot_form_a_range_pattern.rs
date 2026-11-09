//@ compile-fail: range pattern
struct Marker;

fn main() {
    match Marker {
        Marker..=Marker => (),
        _ => (),
    }
}
