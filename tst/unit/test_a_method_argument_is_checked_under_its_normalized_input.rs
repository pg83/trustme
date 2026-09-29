// async-lock arms its lock future with `this.acquire_slow.set(Some(AcquireSlow::new(this.mutex)))`,
// `this.mutex: &mut &Mutex<T>`. rustc confirms a method before its arguments:
// the receiver is unified with the signature, the signature normalized -
// `Pin<&mut Option<X>>::set` takes `Option<X>` - and each argument checked under
// its input, which `Some(..)` hands on to `AcquireSlow::new` as `B = &Mutex<T>`:
// the argument reborrows through the `&mut`.
use std::borrow::Borrow;
use std::marker::PhantomData;
use std::pin::Pin;

struct Mutex<T: ?Sized>(T);

struct Slow<B: Borrow<Mutex<T>>, T: ?Sized>(B, PhantomData<*const T>);

impl<B: Borrow<Mutex<T>>, T: ?Sized> Slow<B, T> {
    fn new(mutex: B) -> Self {
        Slow(mutex, PhantomData)
    }
}

fn arm<'a, T: ?Sized>(mutex: &mut &'a Mutex<T>, mut slot: Pin<&mut Option<Slow<&'a Mutex<T>, T>>>) {
    slot.set(Some(Slow::new(mutex)));
}

fn main() {
    let m = Mutex(5u32);
    let mut r = &m;
    let mut slot = None;
    arm(&mut r, Pin::new(&mut slot));
    assert_eq!(slot.map(|s| s.0 .0), Some(5));
}
