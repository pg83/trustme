struct Value<'a>(&'a u32);

impl<'outer> Value<'outer> {
    fn select(self: Value<'outer>, input: &u32) -> &u32 {
        let _ = self;
        input
    }
}

fn main() {}
