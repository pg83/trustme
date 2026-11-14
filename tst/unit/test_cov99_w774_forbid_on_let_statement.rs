fn main() {
    #[forbid(unsafe_code)]
    let answer = 40 + 2;
    assert_eq!(answer, 42);
}
