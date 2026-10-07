// pest (miette's `diagnostic_chain.rs`) calls `e.fmt(f)` on `e: &&dyn Error`
// inside `impl Debug` and inside `impl Display`. Neither trait is imported.
// Inside a trait impl the trait being implemented is in scope, so at the
// first autoderef step the autoref'd `<&&dyn Error as Debug>::fmt` (or
// `Display`) applies. The object's own `Debug::fmt` and `Display::fmt` take
// `Self = dyn Error` and would only match one step later, where both apply.
// A trait object's methods were assembled at every step with `Self` taken
// from that step's receiver, and the call was reported ambiguous.
use std::error::Error;

#[derive(Debug)]
struct E;

impl std::fmt::Display for E {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("display")
    }
}

impl Error for E {}

enum K<'a> {
    S(&'a (dyn Error + 'a)),
}

impl std::fmt::Debug for K<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            K::S(e) => e.fmt(f),
        }
    }
}

impl std::fmt::Display for K<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            K::S(e) => e.fmt(f),
        }
    }
}

fn main() {
    let k = K::S(&E);
    assert_eq!(format!("{:?}", k), "E");
    assert_eq!(format!("{}", k), "display");
}
