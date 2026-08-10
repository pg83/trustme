// Extracted from library/core/src/ops/index.rs:14
#![allow(unused)]
fn main() {
    use std::ops::Index;

    enum Nucleotide {
        A,
        C,
        G,
        T,
    }

    struct NucleotideCount {
        a: usize,
        c: usize,
        g: usize,
        t: usize,
    }

    impl Index<Nucleotide> for NucleotideCount {
        type Output = usize;

        fn index(&self, nucleotide: Nucleotide) -> &Self::Output {
            match nucleotide {
                Nucleotide::A => &self.a,
                Nucleotide::C => &self.c,
                Nucleotide::G => &self.g,
                Nucleotide::T => &self.t,
            }
        }
    }

    let nucleotide_count = NucleotideCount {a: 14, c: 9, g: 10, t: 12};
    assert_eq!(nucleotide_count[Nucleotide::A], 14);
    assert_eq!(nucleotide_count[Nucleotide::C], 9);
    assert_eq!(nucleotide_count[Nucleotide::G], 10);
    assert_eq!(nucleotide_count[Nucleotide::T], 12);
}
