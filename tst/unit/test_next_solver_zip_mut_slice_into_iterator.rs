use std::mem::MaybeUninit;

struct PolymorphicIter<D: ?Sized> {
    data: D,
}

impl<T> PolymorphicIter<[MaybeUninit<T>]> {
    fn as_slice(&self) -> &[T] {
        unsafe { self.data.assume_init_ref() }
    }
}

impl<T: Clone, const N: usize> Clone for PolymorphicIter<[MaybeUninit<T>; N]> {
    fn clone(&self) -> Self {
        let mut target = Self { data: [const { MaybeUninit::uninit() }; N] };
        clone_into_new(self, &mut target);
        target
    }
}

fn clone_into_new<T: Clone>(
    source: &PolymorphicIter<[MaybeUninit<T>]>,
    target: &mut PolymorphicIter<[MaybeUninit<T>]>,
) {
    for (src, dst) in std::iter::zip(source.as_slice(), &mut target.data) {
        dst.write(src.clone());
    }
}

fn main() {
    let source = PolymorphicIter { data: [MaybeUninit::new(String::from("value"))] };
    let mut target = source.clone();
    let value = unsafe { target.data[0].assume_init_read() };
    assert_eq!(value, "value");
}
