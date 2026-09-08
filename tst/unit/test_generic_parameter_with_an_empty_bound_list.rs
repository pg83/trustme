//@ run-pass
/* combine's `tuple_parser!`: `impl<Input: Stream, $h:, $($id:),*> Parser<Input> for ($h, $($id),*)`
   - generic parameters with an empty bound list after the colon, the last one followed by `>`
   (the parser took `,`, `{` and `;` as ending an empty list, not `>`, `>=` or `=`). */
struct Pair<A:, B: Clone>(A, B);

impl<A:, B: Clone> Pair<A, B> {
    fn second(&self) -> B {
        self.1.clone()
    }
}

fn id<T:>(t: T) -> T {
    t
}

struct Dflt<T: = u8>(T);

fn main() {
    let p = Pair(1u8, String::from("x"));
    assert_eq!(p.second(), "x");
    assert_eq!(id(3), 3);
    assert_eq!(Dflt(4).0, 4u8);
}
