fn main() {
    let text = if true { "w15" } else { Default::default() };
    assert_eq!(text.len(), 3);
}
