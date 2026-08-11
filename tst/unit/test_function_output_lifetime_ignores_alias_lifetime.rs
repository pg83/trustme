struct Value<'a>(&'a u32);
type Alias<'a> = Value<'a>;

impl<'outer> Alias<'outer> {
    fn select(self: Alias<'outer>, input: &u32) -> &u32 {
        let _ = self;
        input
    }
}

fn main() {}
