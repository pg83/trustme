#[must_use]
fn required_value() -> u32 {
    7
}

#[allow(unused_must_use)]
fn main() {
    required_value();
}
