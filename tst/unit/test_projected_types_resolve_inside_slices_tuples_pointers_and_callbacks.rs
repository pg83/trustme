//@ extra-flags: -O

trait Family { type Item: Copy + PartialEq + std::fmt::Debug + 'static; }
struct Words;
impl Family for Words { type Item = u16; }

struct Forms<T: Family> {
    borrowed: &'static [T::Item],
    pair: (T::Item, &'static T::Item),
    pointer: *const T::Item,
    callback: fn(T::Item, &'static T::Item) -> T::Item,
}

fn check<T: Family>(forms: Forms<T>, expected: T::Item) {
    assert_eq!(forms.borrowed[0], forms.pair.0);
    assert!(std::ptr::eq(forms.pointer, forms.pair.1));
    assert_eq!((forms.callback)(forms.pair.0, forms.pair.1), expected);
}

static BASE: u16 = 300;
static VALUES: [u16; 2] = [7, 9];

fn main() {
    check::<Words>(Forms {
        borrowed: &VALUES,
        pair: (7, &BASE),
        pointer: &BASE,
        callback: |left, right| left + *right,
    }, 307);
}
