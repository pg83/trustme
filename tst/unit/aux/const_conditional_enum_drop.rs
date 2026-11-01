#![feature(const_trait_impl, const_destruct)]
struct Guard<'a>(&'a mut u16, u16);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0 += self.1; } }
enum Slot<'a> { Empty, Full(Guard<'a>, Guard<'a>) }
#[inline(never)]
pub const fn total(take_whole: bool, take_field: bool, empty: bool) -> u16 {
    let mut first = 0;
    let mut second = 0;
    {
        let slot = if empty { Slot::Empty } else { Slot::Full(Guard(&mut first, 7), Guard(&mut second, 13)) };
        if take_whole {
            let _whole = slot;
        } else if take_field {
            if let Slot::Full(first, _) = slot { let _first = first; }
        }
    }
    first + second
}
