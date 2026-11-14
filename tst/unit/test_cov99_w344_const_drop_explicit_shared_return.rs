//@ compile-flags: -Z dump-mir
#![feature(const_trait_impl, const_destruct)]

struct Guard<'a>(&'a mut u8);

impl const Drop for Guard<'_> {
    fn drop(&mut self) {
        *self.0 += 1;
    }
}

const fn duplicate<'a>(value: &'a u8, drops: &mut u8) -> (&'a u8, &'a u8) {
    let _guard = Guard(drops);
    return (value, value);
}

const fn evaluate() -> (u8, u8) {
    let mut value = 20u8;
    value += 1;
    let mut drops = 0;
    let pair = duplicate(&value, &mut drops);
    (*pair.0 + *pair.1, drops)
}

const RESULT: (u8, u8) = evaluate();

fn main() {
    assert_eq!(RESULT, (42, 1));
    assert_eq!(evaluate(), RESULT);
}
