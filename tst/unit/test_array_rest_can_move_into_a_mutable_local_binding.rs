//@ compile-variant: -O
fn main() {
    let [first, mut middle @ .., last] = [2u8, 3, 5, 7, 11];
    middle[0] += first;
    middle[2] += last;
    assert_eq!(middle, [5, 5, 18]);
}
