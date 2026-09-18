/* tokio-io-0.1.13 `framed_write.rs`'s `try_ready!(self.poll_complete())`, which expands
   to `Err(e) => return Err(From::from(e))` and so asks for
   `<T as Encoder>::Error: From<<T as Encoder>::Error>`.  Only the reflexive
   `impl<T> From<T> for T` proves that; the item bound `Encoder::Error: From<io::Error>`
   is about another type entirely.

   Upstream `assemble_candidates_from_projected_tys` keeps a bound read off the item
   bounds of a rigid projection only when `match_normalize_trait_ref` equates it with
   the goal after normalization, and `io::Error` does not equate with the rigid
   `<T as Encoder>::Error`, so no such candidate is assembled and the impl is the
   single answer.  Relating the bound as a type's own structure instead left it waiting
   on `<T as Encoder>::Error == io::Error`, and the goal came out ambiguous - "type
   annotations needed" - even though an impl proved it outright. */

use std::io;

trait Encoder {
    type Error: From<io::Error>;
}

struct MyEnc;
#[derive(Debug, PartialEq)]
struct MyErr;
impl From<io::Error> for MyErr {
    fn from(_: io::Error) -> Self { MyErr }
}
impl Encoder for MyEnc {
    type Error = MyErr;
}

fn close<T: Encoder>(r: Result<(), T::Error>) -> Result<(), T::Error> {
    match r {
        Ok(t) => Ok(t),
        Err(e) => return Err(From::from(e)),
    }
}

fn main() {
    let v: Result<(), MyErr> = close::<MyEnc>(Err(MyErr));
    assert_eq!(v, Err(MyErr));
    let v2: Result<(), MyErr> = close::<MyEnc>(Ok(()));
    assert!(v2.is_ok());
}
