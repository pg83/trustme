fn answer() -> usize {
    let _never = return 42;
}

fn main() {
    assert_eq!(answer(), 42);
}
