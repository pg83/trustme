//@ edition: 2024
//@ compile-variant: -O
fn main() {
    let [mut middle @ ..] = [1u32, 2, 3];
    middle[1] *= 4;
    assert_eq!(middle, [1, 8, 3]);
}
