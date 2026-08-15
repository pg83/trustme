trait Value {
    fn value(recurse: bool) -> impl Sized {
        if recurse {
            let _: u32 = Self::value(false);
        }
        0u32
    }
}

fn main() {}
