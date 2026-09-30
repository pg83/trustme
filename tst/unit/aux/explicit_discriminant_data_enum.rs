#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Marker {
    FixPos(u8) = 0x00,
    FixNeg(i8) = 0xe0,
    FixMap(u8) = 0x80,
    Null = 0xc0,
    Reserved,
    False,
}

pub fn is_null(marker: Marker) -> bool {
    marker == Marker::Null
}

pub fn fix_map(n: u8) -> Marker {
    Marker::FixMap(n)
}

pub fn tag(marker: &Marker) -> u8 {
    unsafe { *(marker as *const Marker as *const u8) }
}
