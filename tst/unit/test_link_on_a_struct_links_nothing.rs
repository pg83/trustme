// geographiclib 0.1: `#[link]` written above a struct instead of an extern
// block. Upstream only warns that it belongs on an extern block; the struct
// is an ordinary item and no library is linked.
#[link(name = "geographiclib", kind = "static")]
#[repr(C)]
pub struct Geodesic {
    pub a: f64,
}

fn main() {
    let g = Geodesic { a: 6378137.0 };
    assert_eq!(g.a, 6378137.0);
}
