//@ compile-variant: -O
#![feature(const_trait_impl, const_destruct)]

struct Guard<'a>(&'a mut u32);
impl const Drop for Guard<'_> {
    fn drop(&mut self) { *self.0 += 7; }
}
struct Bundle<'a> { guard: Guard<'a>, tag: u8 }

const fn selected() -> u32 {
    let mut value = 5u32;
    {
        let _bundle = Bundle { guard: Guard(&mut value), tag: 17 };
    }
    value
}

const VALUE: u32 = selected();

fn main() {
    assert_eq!(VALUE, 12);
    assert_eq!(selected(), 12);
}
