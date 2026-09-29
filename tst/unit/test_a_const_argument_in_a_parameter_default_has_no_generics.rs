//@ run-pass
// crc declares `type DefaultImpl = Table<1>` and `struct Crc<W, I = DefaultImpl>`,
// and its tests write `Crc<u32>`. Under min_const_generics an anonymous
// constant that is a const argument has no generic parameters (`generics_of`:
// `AnonConstKind::MCG` has no parent; only a repeat count keeps its parent's
// generics). The `1` was given the generics of whichever item it was met in -
// the struct `Crc<W, I>` once the alias was expanded into the default - and
// filling in `Crc<u32>`'s default from `<u32>` alone indexed `I` out of range.
pub struct Table<const L: usize> {}

type DefaultImpl = Table<1>;

pub struct Crc<W, I = DefaultImpl> {
    width: W,
    table: I,
}

pub struct Direct<W, I = Table<2>, A = [W; 3]> {
    width: W,
    table: I,
    array: A,
}

fn repeated<T: Copy, const N: usize>(value: T) -> [T; N] {
    [value; N]
}

mod test {
    use super::*;

    pub fn check() -> u32 {
        const CRC: Crc<u32> = Crc { width: 1, table: Table {} };
        const DIRECT: Direct<u32> = Direct { width: 2, table: Table {}, array: [3, 4, 5] };
        let _ = (CRC.table, DIRECT.table);
        CRC.width + DIRECT.width + DIRECT.array[2]
    }
}

mod uses_after_the_struct {
    use super::*;

    pub fn check() -> u32 {
        const CRC: Crc<u32> = Crc { width: 10, table: Table {} };
        const DIRECT: Direct<u32> = Direct { width: 20, table: Table {}, array: [30, 40, 50] };
        let _ = (CRC.table, DIRECT.table);
        CRC.width + DIRECT.width + DIRECT.array[2]
    }
}

fn main() {
    assert_eq!(test::check(), 8);
    assert_eq!(uses_after_the_struct::check(), 80);
    assert_eq!(repeated::<u8, 4>(7), [7; 4]);
}
