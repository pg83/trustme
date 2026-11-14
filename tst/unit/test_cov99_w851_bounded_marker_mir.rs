//@ compile-only
//@ compile-flags: -Zdump-mir -Zstop-after=mir
//@ output-contains: _3_mir.rs Copy

struct BoundedMarker<T>(T);

unsafe impl<T: Copy> Send for BoundedMarker<T> {}

fn main() {}
