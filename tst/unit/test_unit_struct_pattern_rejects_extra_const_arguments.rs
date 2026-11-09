//@ compile-fail: Incorrect value parameter count

struct Marker<const N: usize>;

fn main() {
    match Marker::<1> {
        Marker::<1, 2> => {}
    }
}
