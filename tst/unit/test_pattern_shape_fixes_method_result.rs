// The pattern is the only thing that says what `text.parse()` returns, and the
// method cannot resolve until it is told. A nested pattern waits for anything
// else that owns its input to speak first, and the pending method call counts
// as such an owner - but that call is waiting on this very type, so neither
// moved. A pattern that names a type constructor is not a guess about which
// type could match: `Ok(..)` matches Result and nothing else.

fn main() {
    let text = String::from("42");
    let matched = match text.parse() {
        Ok(42i32) => true,
        _ => false,
    };
    assert!(matched);
}
