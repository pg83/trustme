// An item stripped by `cfg` still has to parse: the checks that reject it run
// later. Two forms failed outright — an impl const without a value, and an
// empty `#[repr()]` list.
//
// Same shapes as the gccrs tests issue-2665.rs and issue-3606.rs.
#[repr()]
pub struct Coord {
    x: u32,
    y: u32,
}

struct X;

#[cfg(FALSE)]
impl X {
    const Y: u8;
}

impl X {
    const Y: u8 = 7;
}

fn main() {
    let c = Coord { x: 1, y: 2 };
    assert_eq!(c.x + c.y, 3);
    assert_eq!(X::Y, 7);
}
