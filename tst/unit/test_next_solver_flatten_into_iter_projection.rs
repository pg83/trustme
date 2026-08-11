//@ check-pass
//@ compile-flags: -Znext-solver

struct Escape(Option<char>);

impl Iterator for Escape {
    type Item = char;

    fn next(&mut self) -> Option<char> {
        self.0.take()
    }
}

fn escape(value: char) -> Escape {
    Escape(Some(value))
}

fn flatten_optional_escape(first: Option<char>) -> impl Iterator<Item = char> {
    first
        .map(escape)
        .into_iter()
        .flatten()
        .chain(std::iter::empty())
}

fn main() {
    assert_eq!(flatten_optional_escape(Some('x')).collect::<String>(), "x");
}
