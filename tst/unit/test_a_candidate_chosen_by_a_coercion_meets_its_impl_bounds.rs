// munge_macro (under generic-array's tests) unzips `(&x.0, (&x.1, i))` pairs
// of a non-`Copy` `TokenStream` into `Vec<_>`s. `Vec<?T>: Extend<?A>` has
// `Extend<T> for Vec<T>` and `Extend<&'a T> for Vec<T> where T: Copy`.
// Upstream has `?A = &TokenStream` already - the closure's tail was coerced
// into its fresh return type, a unification - so the second impl asks
// `TokenStream: Copy` and drops out. Here the tail's coercion is still pending
// and only guides the pick: `&TokenStream` into the second impl's `&T` looked
// the better match, `T = TokenStream`, and `TokenStream: Copy` failed later.
// A candidate the coercion instantiates must still meet its impl's bounds.
#[derive(Debug, PartialEq)]
struct Tok(String);

fn render(parsed: &[(Tok, Tok)]) -> String {
    let (bindings, (exprs, indices)) = parsed
        .iter()
        .enumerate()
        .map(|(i, x)| (&x.0, (&x.1, i)))
        .unzip::<_, _, Vec<_>, (Vec<_>, Vec<_>)>();
    let mut out = String::new();
    for ((b, e), i) in bindings.iter().zip(exprs.iter()).zip(indices.iter()) {
        out.push_str(&format!("{}{}{};", b.0, e.0, i));
    }
    out
}

fn main() {
    let parsed = vec![(Tok("a".into()), Tok("b".into())), (Tok("c".into()), Tok("d".into()))];
    assert_eq!(render(&parsed), "ab0;cd1;");
}
