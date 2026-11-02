//@ aux-build: impl_metadata_members.rs
//@ compile-variant: -O
fn main() {
    let text = String::from("abc");
    assert_eq!(impl_metadata_members::consume(move |value| value + text.len() as u32), 8);
    let mut state = 0;
    assert_eq!(impl_metadata_members::repeat(|value| { state += value; state }), 7);
    assert_eq!(state, 5);
    let captured = 11;
    assert_eq!(impl_metadata_members::shared(|value| value + captured), 18);
}
