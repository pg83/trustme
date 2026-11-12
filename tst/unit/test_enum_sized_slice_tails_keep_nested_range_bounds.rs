#[repr(u8)]
enum Width {
    Start = 2,
    End = 5,
}

const COUNT: usize = Width::End as usize;

struct Frame<T: ?Sized> {
    stamp: u8,
    values: T,
}

fn main() {
    let frame = Frame {
        stamp: Width::Start as u8,
        values: [1303u16; COUNT],
    };
    let view: &Frame<[u16]> = &frame;
    let bounded = .. ..=COUNT;
    let unbounded = .. ..;
    assert_eq!(view.stamp, 2);
    assert_eq!(view.values.len(), bounded.end.end);
    assert_eq!(view.values[COUNT - 1], 1303);
    assert_eq!(unbounded.end, core::ops::RangeFull);
    assert_eq!(usize::from_le_bytes(COUNT.to_le_bytes()), 5);
}
