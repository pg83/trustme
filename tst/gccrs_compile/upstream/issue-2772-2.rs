// { dg-options "-w" }

#![feature(lang_items)]
struct Pair<'a, T, U>
where
    T: 'a,
    U: 'a,
{
    left: T,
    right: U,
    marker: std::marker::PhantomData<&'a ()>,
}

pub fn test<'a>() {
    let a: i32 = 50;
    let x = Pair::<&'_ _, &'_ _> {
        left: &&a,
        right: &a,
        marker: std::marker::PhantomData,
    };
}
