// `gen` is a keyword only from edition 2024: upstream opens a `gen` block
// when the `gen` token itself is 2024 (`token_uninterpolated_span()
// .at_least_rust_2024()`), so in 2021 `gen { x: 3 }` is a struct literal.
// The check read the edition of the token taken before `gen`, which at the
// start of `assert!`'s condition stream was never set, against 2021.
#[allow(non_camel_case_types)]
struct gen { x: u8 }
fn main() {
    let g = gen { x: 3 };
    assert_eq!(g.x, 3);
    assert!(gen { x: 4 }.x == 4);
}
