fn opaque_iterator() -> impl Iterator {
    0..1
}

fn main() {
    let _ = opaque_iterator().map(|value| value);
}
