// Two types are equal only if their const arguments are: `Bump<{?N}>` and
// `Bump<MIN_ALIGN>` still have `?N = MIN_ALIGN` to settle. The inference
// equality compared the type arguments of a path and skipped the values, so
// coercing `self: &mut &Bump<MIN_ALIGN>` to `&Bump<{?N}>` through a deref
// took the two as one type, recorded no equality, and `?N` stayed unknown
// ("Failure to infer") - bumpalo's `Bump::shrink(self, ..)` inside
// `impl Alloc for &Bump<MIN_ALIGN>`.
pub struct Bump<const MIN_ALIGN: usize = 1> {
    base: u8,
}

impl<const MIN_ALIGN: usize> Bump<MIN_ALIGN> {
    fn shrink(&self, amount: u8) -> u8 {
        self.base + amount + MIN_ALIGN as u8
    }
}

trait Alloc {
    fn realloc(&mut self, amount: u8) -> u8;
}

impl<'a, const MIN_ALIGN: usize> Alloc for &'a Bump<MIN_ALIGN> {
    fn realloc(&mut self, amount: u8) -> u8 {
        Bump::shrink(self, amount)
    }
}

fn through_deref<const N: usize>(bump: &mut &Bump<N>) -> u8 {
    Bump::shrink(bump, 2)
}

fn main() {
    let bump: Bump<4> = Bump { base: 10 };
    let mut handle = &bump;
    assert_eq!(handle.realloc(1), 15);
    assert_eq!(through_deref(&mut &bump), 16);
}
