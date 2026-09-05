// Deriving `PartialOrd` compares `&isize` fields through
// `impl<A: PartialOrd<B>, B> PartialOrd<&B> for &A`, whose own where-clause
// selects the same impl one level down.  Every instantiation of an impl is its
// own set of unknowns - upstream draws fresh inference variables for each - so
// the inner `B` is not the outer one.  Sharing them answered `B == &B`, a type
// that contains itself, and typecheck aborted on it.

#[derive(Eq, PartialEq, PartialOrd, Ord)]
enum Test<'a> {
    Int(&'a isize),
    Slice(&'a [u8]),
}

fn main() {
    let low = 1isize;
    let high = 2isize;
    let bytes = [3u8, 4];
    assert!(Test::Int(&low) < Test::Int(&high));
    assert!(Test::Int(&high) < Test::Slice(&bytes));
}
