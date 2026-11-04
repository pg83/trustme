pub enum Entry { Empty, First(u16), Second(u16), Last }

#[inline]
pub fn classify<T>(entry: Entry) -> usize {
    let bias = core::mem::size_of::<T>();
    bias + match entry {
        Entry::Empty => 3,
        Entry::First(value) => value as usize + 5,
        Entry::Second(value) => value as usize + 7,
        Entry::Last => 11,
    }
}

#[inline]
pub fn integer<T>(value: i16) -> usize {
    core::mem::size_of::<T>() + match value {
        -37 => 13,
        -5 => 17,
        19 => 23,
        41 => 29,
        _ => 31,
    }
}

pub const fn constant(value: u8) -> usize {
    match value { 2 => 37, 7 => 41, 13 => 43, 23 => 47, _ => 53 }
}
