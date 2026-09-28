// odht's quickcheck over `Entry<[u8; 0], [u8; 0]>` checks that its entry slice
// ends where the metadata starts; `as_ptr_range()` of a slice of a zero-sized
// type ended `len` bytes past its start. `ptr.add(n)` is `offset`, a move by
// `n * size_of::<T>()` bytes, so for a zero-sized `T` it is the pointer itself;
// the C backend wrote plain pointer arithmetic, and a C++ empty struct is one
// byte wide.
#[derive(Clone, Copy)]
struct Unit;

#[derive(Clone, Copy)]
#[allow(dead_code)]
struct Entry<K, V> {
    key: K,
    value: V,
}

fn stride<T: Copy>(value: T) -> usize {
    let values = [value; 4];
    let start = values.as_ptr();
    let range = values.as_ptr_range();
    assert_eq!(range.end as usize, unsafe { start.add(4) } as usize);
    unsafe { start.add(4) as usize - start as usize }
}

fn main() {
    assert_eq!(stride(Unit), 0);
    assert_eq!(stride(()), 0);
    assert_eq!(stride([0u8; 0]), 0);
    assert_eq!(stride(Entry { key: [0u8; 0], value: [0u8; 0] }), 0);
    assert_eq!(stride(7u16), 8);
    let units = [Unit; 3];
    let first = units.as_ptr();
    assert_eq!(unsafe { first.offset(2) } as usize, first as usize);
    assert_eq!(first.wrapping_add(5) as usize, first as usize);
}
