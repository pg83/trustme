// A float literal reaches `concat!` and `stringify!` as text, and the text has
// to read back as the same value without spelling out every digit binary128
// could need.
fn main() {
    assert_eq!(concat!("test", 10, 'b', true, 2.15), "test10btrue2.15");
    assert_eq!(concat!(2.15, 1u64), "2.151");
    assert_eq!(stringify!(4.0), "4.0");
    assert_eq!(stringify!(0.1), "0.1");
}
