//@ compile-fail: Using `..` with a non-value on left

fn main() {
    let _ = match (1u16, 2u16) {
        (left, right)..7 => 1,
        _ => 0,
    };
}
