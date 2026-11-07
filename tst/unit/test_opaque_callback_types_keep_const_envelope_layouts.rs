#![feature(type_alias_impl_trait)]

type Hidden = impl Copy;

#[define_opaque(Hidden)]
const fn construct() -> Hidden {
    [149u16, 151, 157]
}

type Callback = fn() -> Hidden;

#[repr(C)]
struct Envelope<F> {
    tag: u8,
    callback: F,
    end: u16,
}

const END_OFFSET: usize = std::mem::offset_of!(Envelope<Callback>, end);
const TOTAL_SIZE: usize = std::mem::size_of::<Envelope<Callback>>();

fn main() {
    let envelope: Envelope<Callback> = Envelope {
        tag: 19,
        callback: construct,
        end: 163,
    };
    assert_eq!(END_OFFSET, std::mem::offset_of!(Envelope<fn()>, end));
    assert_eq!(TOTAL_SIZE, std::mem::size_of::<Envelope<fn()>>());
    assert_eq!(std::mem::size_of_val(&(envelope.callback)()), 6);
    assert_eq!((envelope.tag, envelope.end), (19, 163));
}
