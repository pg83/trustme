struct Parcel<const N: usize>([u16; N]);

enum Frame<const N: usize> {
    Body([u16; N]),
}

struct Width<const N: usize>;

impl<const N: usize> Width<N> {
    const VALUE: usize = N;

    fn array<const M: usize>(value: u16) -> [u16; M] {
        [value; M]
    }
}

trait Make<const N: usize> {
    fn array<const M: usize>(value: u16) -> [u16; M];
}

impl<const N: usize> Make<N> for Width<N> {
    fn array<const M: usize>(value: u16) -> [u16; M] {
        [value; M]
    }
}

fn main() {
    let constructor = Parcel::<{ Width::<3>::VALUE }>;
    let Parcel(first) = constructor(Width::<1>::array::<{ Width::<3>::VALUE }>(31));
    assert_eq!(first, [31; 3]);

    let variant = Frame::<{ Width::<5>::VALUE }>::Body;
    let Frame::Body(second) = variant(<Width<2> as Make<2>>::array::<{ Width::<5>::VALUE }>(37));
    assert_eq!(second, [37; 5]);
}
