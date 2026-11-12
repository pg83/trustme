trait Source {
    type Output;
}

trait Limit {
    const END: u16;
}

struct Marker;

impl Source for Marker {
    type Output = u16;
}

impl Limit for u16 {
    const END: u16 = 1301;
}

fn main() {
    let absolute = 0u16..::core::primitive::u16::MAX;
    let projected = 7u16..<<Marker as Source>::Output as Limit>::END;
    assert_eq!(absolute.start, 0);
    assert_eq!(absolute.end, 65535);
    assert_eq!(projected.start, 7);
    assert_eq!(projected.end, 1301);
}
