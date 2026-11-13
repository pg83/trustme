//@ compile-fail: Couldn't find the field 0

struct Marker;

fn main() {
    let _ = Marker.0;
}
