#![feature(f16, f128)]

pub fn keep_wide(_half: f16, wide: f128) -> f128 {
    wide
}
