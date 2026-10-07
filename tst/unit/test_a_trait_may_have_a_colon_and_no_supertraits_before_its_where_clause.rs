// shellexpand 3.1.2: `trait WstrExt: where for<'s> &'s Self: WstrRefExt`. rustc's
// parse_item_trait reads the bounds after `:` with parse_generic_bounds, which
// takes none when the next token cannot start a bound.
pub trait Letters {
    fn letters(self) -> usize;
}

impl Letters for &str {
    fn letters(self) -> usize {
        self.chars().count()
    }
}

pub trait Measured: where for<'s> &'s Self: Letters {
    fn measure(&self) -> usize {
        self.letters()
    }
}

impl Measured for str {}

fn main() {
    assert_eq!("abcd".measure(), 4);
}
