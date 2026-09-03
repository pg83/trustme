// A struct pattern names the type it matches, which is how `let Inner(value) =
// Default::default()` knows what to default. A pattern nested inside another
// names its type just the same, but only the outermost one was read that way:
// the inner field's type stayed open, and the call it came from could not be
// resolved.

struct Inner(usize);

impl Default for Inner {
    fn default() -> Inner {
        Inner(42)
    }
}

struct Outer<T>(T);

impl<T: Default> Default for Outer<T> {
    fn default() -> Outer<T> {
        Outer(Default::default())
    }
}

fn main() {
    let Outer(Inner(value)) = Default::default();
    assert_eq!(value, 42);
}
