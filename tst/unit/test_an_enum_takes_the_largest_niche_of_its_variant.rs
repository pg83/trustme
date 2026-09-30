// time's meta tests pin `Option<TryFromParsed>` at 24 bytes and
// `Option<Component>` at the size of `Component`. Upstream's layout gives a
// struct the largest niche among its fields (`largest_niche`, by the count of
// values it has free) and an enum the largest niche of its largest variant.
// We took the first niche that fitted: the null of the `&str` in
// `ComponentRange`, which leaves no value for an outer `Option`, and the tag of
// the first `Pad` where the enum itself had used a `bool`. An enum that holds
// another gets the niche that one used, with the values it left free. The
// null of a pointer, or the zero below an enum's first discriminant, is still
// where `None` goes when that is the largest niche (`Niche::reserve`).
use std::mem::size_of;
use std::num::NonZeroU16;

#[allow(dead_code)]
pub struct ComponentRange {
    name: &'static str,
    is_conditional: bool,
}

#[allow(dead_code)]
pub enum TryFromParsed {
    InsufficientInformation,
    ComponentRange(ComponentRange),
}

#[allow(dead_code)]
pub enum Pad {
    None,
    Zero,
    Space,
}

#[allow(dead_code)]
pub struct Day {
    padding: Pad,
}

#[allow(dead_code)]
pub struct Year {
    padding: Pad,
    repr: Pad,
    iso: bool,
    sign: bool,
}

#[allow(dead_code)]
pub enum Component {
    Day(Day),
    Year(Year),
    Hour(Day),
    Ignore(u16),
    End,
}

#[allow(dead_code)]
pub enum Range {
    Standard,
    Extended,
}

#[allow(dead_code)]
pub struct FullYear {
    padding: Pad,
    repr: Pad,
    range: Range,
    iso: bool,
    sign: bool,
}

#[allow(dead_code)]
pub struct Month {
    padding: Pad,
    repr: Pad,
    case_sensitive: bool,
}

#[allow(dead_code)]
pub enum Wide {
    Month(Month),
    Year(FullYear),
    Ignore(NonZeroU16),
    End,
}

#[allow(dead_code)]
#[repr(u8)]
pub enum Size {
    One = 1,
    Two = 2,
    Three = 3,
}

fn main() {
    assert_eq!(size_of::<TryFromParsed>(), 24);
    assert_eq!(size_of::<Option<TryFromParsed>>(), 24);
    assert_eq!(size_of::<Option<Option<TryFromParsed>>>(), 24);
    assert_eq!(size_of::<Component>(), 4);
    assert_eq!(size_of::<Option<Component>>(), 4);
    assert_eq!(size_of::<Wide>(), 6);
    assert_eq!(size_of::<Option<Wide>>(), 6);
    assert_eq!(size_of::<Option<Option<(&u8, bool)>>>(), 16);
    assert_eq!(size_of::<Option<&u8>>(), 8);
    assert_eq!(size_of::<Option<Option<bool>>>(), 1);
    assert_eq!(0, unsafe { std::mem::transmute::<Option<Size>, u8>(None) });
    assert_eq!(0, unsafe { std::mem::transmute::<Option<&u8>, usize>(None) });
}
