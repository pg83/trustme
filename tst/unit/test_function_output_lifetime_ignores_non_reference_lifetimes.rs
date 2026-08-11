struct Value<'a>(&'a str);

impl<'outer> Value<'outer> {
    fn select<'input>(self: Value<'outer>, input: &'input str) -> &str {
        let _ = self;
        input
    }
}

fn main() {}
