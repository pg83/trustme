#![feature(specialization)]
#![allow(incomplete_features)]

trait Rank { const VALUE: u8; }
struct Wrap<T>(T);

macro_rules! ranks {
    ($wide:ty, $middle:ty, $leaf:ty) => {
        impl Rank for $leaf { const VALUE: u8 = 3; }
        impl<T> Rank for $middle { default const VALUE: u8 = 2; }
        impl<T> Rank for $wide { default const VALUE: u8 = 1; }
    };
}

ranks!(Wrap<T>, Wrap<(T,)>, Wrap<(u8,)>);
ranks!((T, u8), ((T,), u8), ((u8,), u8));
ranks!([T; 2], [(T,); 2], [(u8,); 2]);
ranks!([T], [(T,)], [(u8,)]);
ranks!(*const T, *const (T,), *const (u8,));
ranks!(*mut T, *mut (T,), *mut (u8,));
ranks!(&T, &(T,), &(u8,));
ranks!(&mut T, &mut (T,), &mut (u8,));

fn main() {
    assert_eq!([Wrap::<u16>::VALUE, Wrap::<(u16,)>::VALUE, Wrap::<(u8,)>::VALUE], [1, 2, 3]);
    assert_eq!([<(u16, u8)>::VALUE, <((u16,), u8)>::VALUE, <((u8,), u8)>::VALUE], [1, 2, 3]);
    assert_eq!([<[u16; 2]>::VALUE, <[(u16,); 2]>::VALUE, <[(u8,); 2]>::VALUE], [1, 2, 3]);
    assert_eq!([<[u16]>::VALUE, <[(u16,)]>::VALUE, <[(u8,)]>::VALUE], [1, 2, 3]);
    assert_eq!([<*const u16>::VALUE, <*const (u16,)>::VALUE, <*const (u8,)>::VALUE], [1, 2, 3]);
    assert_eq!([<*mut u16>::VALUE, <*mut (u16,)>::VALUE, <*mut (u8,)>::VALUE], [1, 2, 3]);
    assert_eq!([<&u16>::VALUE, <&(u16,)>::VALUE, <&(u8,)>::VALUE], [1, 2, 3]);
    assert_eq!([<&mut u16>::VALUE, <&mut (u16,)>::VALUE, <&mut (u8,)>::VALUE], [1, 2, 3]);
}
