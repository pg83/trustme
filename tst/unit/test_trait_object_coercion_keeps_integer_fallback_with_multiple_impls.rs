trait Marker {
    fn tag(&self) -> u32;
}

impl Marker for i32 {
    fn tag(&self) -> u32 { 17 }
}

impl Marker for u64 {
    fn tag(&self) -> u32 { 29 }
}

fn main() {
    let value = 0;
    let view: &dyn Marker = &value;
    assert_eq!(view.tag(), 17);
}
