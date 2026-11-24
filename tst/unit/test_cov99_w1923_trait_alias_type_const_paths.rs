#![feature(trait_alias)]

trait Fold<T> {
    fn fold(&self, input: T) -> usize;
}

trait Bias<const B: usize> {
    fn bias(&self) -> usize {
        B
    }
}

trait Calibrated<T, const B: usize> = Fold<T> + Bias<B>;

struct Gauge(usize);

impl Fold<u16> for Gauge {
    fn fold(&self, input: u16) -> usize {
        self.0 + input as usize
    }
}

impl Bias<13> for Gauge {}

fn read<G: Calibrated<u16, 13>>(gauge: &G) -> usize {
    gauge.fold(17) + gauge.bias()
}

fn main() {
    assert_eq!(read(&Gauge(5)), 35);
}
