pub const HEAD: (f32, f64) = (1.25, -2.5);

#[inline]
pub fn choose(first: bool) -> (f64, f32) {
    if first { (3.75, -0.0) } else { (6.5, 0.5) }
}

#[macro_export]
macro_rules! fraction { () => { -0.125f64 }; }
