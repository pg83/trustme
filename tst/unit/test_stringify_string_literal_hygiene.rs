fn main() {
    let rendered = stringify!("text");

    assert!(rendered.starts_with('"'));
    assert!(rendered.contains("text"));
    assert!(rendered.ends_with('"'));
    assert!(!rendered.contains("/*"));
}
