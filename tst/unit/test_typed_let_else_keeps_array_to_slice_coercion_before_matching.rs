//@ compile-variant: -O
fn first(values: &[u16; 3]) -> u16 {
    let [head, tail @ ..]: &[u16] = values else { return 0 };
    assert_eq!(tail.len(), 2);
    *head
}
fn main() { assert_eq!(first(&[17, 31, 43]), 17); }
