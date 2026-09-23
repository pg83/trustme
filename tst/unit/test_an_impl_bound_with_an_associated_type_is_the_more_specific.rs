// An impl whose bound fixes an associated type specialises one whose bound
// names the bare trait: upstream elaborates `I: Counted<Item = u8>` into
// `I: Counted`, `I: Iterator` and `<I as Iterator>::Item == u8`, and the
// child's predicates imply the parent's. Flattening a bound into its
// supertraits dropped the `Item = u8` that belongs to `Iterator`, which
// only compared equal to the parent's `I: Iterator<Item = u8>` because
// the ordering of bound maps ignored a longer map; with that fixed the
// comparison hit a TODO (liballoc's `TrustedLen<Item = T>` specialisations).
#![feature(specialization)]
#![allow(incomplete_features)]

trait Counted: Iterator {}
impl Counted for std::vec::IntoIter<u8> {}

trait Describe {
    fn describe(&self) -> &'static str;
}

impl<I: Iterator> Describe for I {
    default fn describe(&self) -> &'static str {
        "any iterator"
    }
}

impl<I: Iterator<Item = u8>> Describe for I {
    default fn describe(&self) -> &'static str {
        "bytes"
    }
}

impl<I: Counted<Item = u8>> Describe for I {
    fn describe(&self) -> &'static str {
        "counted bytes"
    }
}

fn main() {
    assert_eq!(vec![1u32].into_iter().describe(), "any iterator");
    assert_eq!(vec![1u8].iter().copied().describe(), "bytes");
    assert_eq!(vec![1u8].into_iter().describe(), "counted bytes");
}
