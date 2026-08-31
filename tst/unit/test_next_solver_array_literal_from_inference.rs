//@ check-pass
//@ compile-flags: -Znext-solver

fn inferred_array_element() -> Box<[u8]> {
    Box::from([0])
}

fn main() {
    let _ = inferred_array_element();
}
