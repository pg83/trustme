//@ compile-variant: -O
struct Token<const N: usize>;
fn number<const N: usize>(_: Token<N>) -> usize { N }
fn main() { assert_eq!(number(Token::<{ 2 + 3 }>), 5); assert_eq!(number(Token::<7>), 7); }
