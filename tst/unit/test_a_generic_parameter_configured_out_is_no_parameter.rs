// `#[cfg]` on a generic parameter removes it, with the bounds written on
// it, before anything reads the list (upstream's StripUnconfigured
// configures generic params). The attributes were ignored, so petgraph's
// `GraphMap<N, E, Ty, #[cfg(not(feature = "std"))] S,
// #[cfg(feature = "std")] S = RandomState>` had two `S`, one of them unused
// ("parameter `S` is never used in this struct").
use std::marker::PhantomData;

#[derive(Clone, Debug, Default)]
pub struct Map<N, #[cfg(any())] S: Copy, #[cfg(all())] S: Clone = u16> {
    hasher: S,
    nodes: PhantomData<N>,
}

struct Borrowed<#[cfg(any())] 'a, 'b>(&'b u8);

fn pick<#[cfg(any())] T: Iterator, U, #[cfg(any())] const N: usize, const M: usize>(u: U) -> [U; M]
where
    U: Copy,
{
    [u; M]
}

fn main() {
    let map: Map<u8> = Map::default();
    let copy = map.clone();
    assert_eq!(copy.hasher, 0u16);
    assert_eq!(format!("{:?}", copy.nodes), "PhantomData<u8>");
    let value = 7u8;
    assert_eq!(*Borrowed(&value).0, 7);
    assert_eq!(pick::<u32, 3>(5), [5, 5, 5]);
}
