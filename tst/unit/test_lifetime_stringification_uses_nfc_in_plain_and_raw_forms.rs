macro_rules! spell { ($value:lifetime) => { stringify!($value) }; }
fn main() {
    assert_eq!(stringify!('λè̕), "'λè̕");
    assert_eq!(spell!('λè̕), "'λè̕");
    assert_eq!(spell!('r#λè̕), "'r#λè̕");
}
