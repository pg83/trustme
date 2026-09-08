//@ run-pass
/* zerocopy's `pointer::inner::tests::test_trailing_slice`: the derived `KnownLayout` impl of a
   local `SliceDst<const OFFSET: usize>` has `const LAYOUT = DstLayout::for_repr_c_struct(..,
   &[DstLayout::for_type::<[u8; OFFSET]>(), <[u8] as KnownLayout>::LAYOUT])`.  The array behind
   the reference is a temporary promoted to a static of the impl's generics, and upstream
   evaluates such a promoted constant with each instance's substitutions; evaluated once and
   kept on the item, `SliceDst<0>` read the offset of `SliceDst<2>` (14 trailing bytes of 16). */
#[derive(Clone, Copy)]
struct DstLayout {
    offset: usize,
}

impl DstLayout {
    const fn for_type<T>() -> DstLayout {
        DstLayout { offset: core::mem::size_of::<T>() }
    }
    const fn for_repr_c_struct(_align: Option<usize>, fields: &[DstLayout]) -> DstLayout {
        let mut offset = 0;
        let mut i = 0;
        while i < fields.len() {
            offset += fields[i].offset;
            i += 1;
        }
        DstLayout { offset }
    }
}

trait KnownLayout {
    const LAYOUT: DstLayout;
}

impl KnownLayout for [u8] {
    const LAYOUT: DstLayout = DstLayout { offset: 0 };
}

fn cast<U: ?Sized + KnownLayout>(bytes: usize) -> usize {
    bytes - U::LAYOUT.offset
}

fn test_trailing_slice<const OFFSET: usize, const BUFFER_SIZE: usize>() {
    #[repr(C)]
    struct SliceDst<const OFFSET: usize> {
        prefix: [u8; OFFSET],
        trailing: [u8],
    }

    impl<const OFFSET: usize> KnownLayout for SliceDst<OFFSET> {
        const LAYOUT: DstLayout = DstLayout::for_repr_c_struct(
            None,
            &[DstLayout::for_type::<[u8; OFFSET]>(), <[u8] as KnownLayout>::LAYOUT],
        );
    }

    let n: usize = BUFFER_SIZE - OFFSET;
    assert_eq!(cast::<SliceDst<OFFSET>>(BUFFER_SIZE), n, "OFFSET={OFFSET}");
}

fn main() {
    test_trailing_slice::<0, 16>();
    test_trailing_slice::<1, 17>();
    test_trailing_slice::<2, 18>();
}
