//@ proc-macro-aux-build: use_item_identity.rs

macro_rules! in_two_modules {
    ($item:item) => {
        mod left { use_item_identity::echo!($item); }
        mod right { use_item_identity::echo!($item); }
    };
}

in_two_modules! {
    #[derive(Clone, Copy)]
    pub struct View<
        'short,
        'long: 'short,
        #[cfg(any())] Removed,
        T: ?Sized + 'long = u16,
        const N: usize = 2,
    > {
        pub value: &'long T,
        pub label: &'short str,
        pub marker: [u8; N],
    }
}

fn main() {
    let number = 37u16;
    let first: left::View<'_, '_> = left::View {
        value: &number,
        label: "east",
        marker: [3, 5],
    };
    let copied = first.clone();
    assert_eq!((*copied.value, copied.label, copied.marker), (37, "east", [3, 5]));

    let numbers = [41u32, 43, 47];
    let second: right::View<'_, '_, [u32], 3> = right::View {
        value: &numbers,
        label: "west",
        marker: [7, 11, 13],
    };
    assert_eq!(second.value, &[41, 43, 47]);
    assert_eq!((second.label, second.marker), ("west", [7, 11, 13]));
}
