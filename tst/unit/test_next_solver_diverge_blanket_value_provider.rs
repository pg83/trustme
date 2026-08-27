#![feature(never_type)]
#![feature(rustc_attrs)]

trait Convert<T> {
    fn convert(value: T) -> Self;
}

impl<T> Convert<T> for T {
    fn convert(value: T) -> T {
        value
    }
}

#[rustc_reservation_impl = "reserved for a future blanket implementation"]
impl<T> Convert<!> for T {
    fn convert(value: !) -> T {
        value
    }
}

// Translation must resolve a value item provided by a generic identity impl
// after substituting the diverging type for its implementation parameter.
fn convert(value: !) -> ! {
    <! as From<!>>::from(value)
}

fn main() {
    let provider: fn(!) -> ! = <! as From<!>>::from;
    let local_provider: fn(!) -> ! = <! as Convert<!>>::convert;
    std::hint::black_box(provider);
    std::hint::black_box(local_provider);
    let _ = convert;
}
