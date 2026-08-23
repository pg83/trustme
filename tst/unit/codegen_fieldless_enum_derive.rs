#[repr(i16)]
#[derive(Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub enum Wide {
    V0 = 100,
    V1 = -7,
    V2 = 42,
    V3 = 0,
    V4 = 91,
    V5 = -99,
    V6 = 13,
    V7 = 88,
}

#[no_mangle]
pub fn trustme_fieldless_eq(left: &Wide, right: &Wide) -> bool {
    left == right
}

#[no_mangle]
pub fn trustme_fieldless_cmp(left: &Wide, right: &Wide) -> core::cmp::Ordering {
    left.cmp(right)
}

#[no_mangle]
pub fn trustme_fieldless_partial_cmp(
    left: &Wide,
    right: &Wide,
) -> Option<core::cmp::Ordering> {
    left.partial_cmp(right)
}
